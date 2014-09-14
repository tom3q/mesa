/*
 * Copyright (C) 2014 Tomasz Figa <tomasz.figa@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <util/u_double_list.h>

#include "openfimg_ir_priv.h"
#include "openfimg_util.h"

/*
 * Construction of defined variable lists.
 */

static void
variables_defined_list(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;

	LIST_FOR_EACH_ENTRY(ins, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;
		unsigned comp;

		if (!dst || dst->type != OF_IR_REG_VAR)
			continue;

		for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
			if (!reg_comp_used(dst, comp))
				continue;

			of_bitmap_set(node->ssa.vars_defined, dst->var[comp]);
		}
	}
}

static void
variables_defined(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child, *parent;
	uint32_t *vars_defined;

	vars_defined = of_heap_alloc(opt->heap, opt->vars_bitmap_size);
	node->ssa.vars_defined = vars_defined;

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		variables_defined(opt, child);

	switch (node->type) {
	case OF_IR_NODE_DEPART:
	case OF_IR_NODE_REPEAT:
		parent = node->depart_repeat.region;
		break;
	case OF_IR_NODE_LIST:
		variables_defined_list(opt, node);
		/* Intentional fall-through. */
	case OF_IR_NODE_REGION:
		parent = node->parent;
		break;
	default:
		return;
	}

	if (parent)
		of_bitmap_or(parent->ssa.vars_defined, parent->ssa.vars_defined,
				vars_defined, opt->num_vars);

	switch (node->type) {
	case OF_IR_NODE_DEPART:
	case OF_IR_NODE_REPEAT:
		memset(vars_defined, 0, opt->vars_bitmap_size);
		break;
	default:
		break;
	}
}

/*
 * Departures/repeats counting.
 */

static void
dep_rep_count(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		dep_rep_count(opt, child);

	switch (node->type) {
	case OF_IR_NODE_DEPART:
		node->ssa.depart_number =
				node->depart_repeat.region->ssa.depart_count++;
		break;
	case OF_IR_NODE_REPEAT:
		node->ssa.repeat_number =
				++node->depart_repeat.region->ssa.repeat_count;
		break;
	default:
		break;
	}
}

/*
 * PHI operator insertion.
 */

static void
make_trivials(struct of_ir_optimizer *opt, struct list_head *list, uint32_t *vars,
	      unsigned count)
{
	struct of_ir_phi *phi;
	unsigned bit;
	unsigned src;

	OF_BITMAP_FOR_EACH_SET_BIT(bit, vars, opt->num_vars) {
		phi = of_heap_alloc(opt->shader_heap, sizeof(*phi)
					+ count * sizeof(*phi->src));
		phi->reg = bit;
		phi->dst = bit;
		for (src = 0; src < count; ++src)
			phi->src[src] = bit;
		list_addtail(&phi->list, list);
	}
}

static void
insert_phi(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		insert_phi(opt, child);

	/* Handles if_then and region nodes. */
	if (node->ssa.depart_count)
		make_trivials(opt, &node->ssa.phis,
				node->ssa.vars_defined, node->ssa.depart_count);

	/* Handles region nodes with repeat subnodes. */
	if (node->ssa.repeat_count)
		make_trivials(opt, &node->ssa.loop_phis,
				node->ssa.vars_defined,
				node->ssa.repeat_count + 1);
}

/*
 * Variable renaming.
 */

static void
rename_phi_operand(struct of_ir_optimizer *opt, unsigned num, struct of_ir_phi *phi,
		   uint16_t *renames)
{
	phi->src[num] = renames[phi->reg];
}

static void
rename_lhs(struct of_ir_optimizer *opt, struct of_ir_phi *phi, uint16_t *renames)
{
	phi->dst = opt->last_var++;
	opt->renames[phi->reg] = phi->dst;
}

static void
rename_operands(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;

	LIST_FOR_EACH_ENTRY(ins, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;
		unsigned comp;
		unsigned i;

		for (i = 0; i < ins->num_srcs; ++i) {
			struct of_ir_register *src = ins->srcs[i];

			if (src->type != OF_IR_REG_VAR)
				continue;

			for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
				if (!reg_comp_used(src, comp))
					continue;
				src->var[comp] = opt->renames[src->var[comp]];
			}
		}

		if (!dst || dst->type != OF_IR_REG_VAR)
			continue;

		for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
			uint16_t var = dst->var[comp];

			if (!reg_comp_used(dst, comp))
				continue;

			dst->var[comp] = opt->last_var++;
			opt->renames[var] = dst->var[comp];
		}
	}
}

static void
make_ssa(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *region;
	struct of_ir_ast_node *child;
	struct of_ir_phi *phi;

	switch (node->type) {
	case OF_IR_NODE_REGION:
		LIST_FOR_EACH_ENTRY(phi, &node->ssa.loop_phis, list) {
			rename_phi_operand(opt, 0, phi, opt->renames);
			rename_lhs(opt, phi, opt->renames);
		}
		break;

	case OF_IR_NODE_DEPART:
	case OF_IR_NODE_REPEAT:
		opt->renames = of_stack_push_copy(opt->renames_stack);
		break;

	case OF_IR_NODE_LIST:
		rename_operands(opt, node);
		return;

	default:
		break;
	}

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		make_ssa(opt, child);

	switch (node->type) {
	case OF_IR_NODE_REGION:
		LIST_FOR_EACH_ENTRY(phi, &node->ssa.phis, list)
			rename_lhs(opt, phi, opt->renames);
		break;

	case OF_IR_NODE_DEPART:
		region = node->depart_repeat.region;
		LIST_FOR_EACH_ENTRY(phi, &region->ssa.phis, list)
			rename_phi_operand(opt, node->ssa.depart_number, phi,
						opt->renames);

		opt->renames = of_stack_pop(opt->renames_stack);
		break;

	case OF_IR_NODE_REPEAT:
		region = node->depart_repeat.region;
		LIST_FOR_EACH_ENTRY(phi, &region->ssa.loop_phis, list)
			rename_phi_operand(opt, node->ssa.repeat_number, phi,
						opt->renames);

		opt->renames = of_stack_pop(opt->renames_stack);
		break;

	default:
		break;
	}
}

/*
 * Node initialization.
 */

static void
init_nodes(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	memset(&node->ssa, 0, sizeof(node->ssa));

	LIST_INITHEAD(&node->ssa.phis);
	LIST_INITHEAD(&node->ssa.loop_phis);

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		init_nodes(opt, child);
}

/*
 * Dumping of stage-specific tree data.
 */

static void
dump_phis(struct list_head *list, unsigned count, unsigned level)
{
	struct of_ir_phi *phi;
	unsigned i;

	LIST_FOR_EACH_ENTRY(phi, list, list) {
		_debug_printf("%*s@%d", level, "", phi->dst);
		_debug_printf(" = PHI(@%d", phi->src[0]);
		for (i = 1; i < count; ++i)
			_debug_printf(", @%d", phi->src[i]);
		_debug_printf(")\n");
	}
}

static void
dump_ssa_data_pre(struct of_ir_shader *shader, struct of_ir_ast_node *node,
		  unsigned level, void *data)
{
	struct of_ir_optimizer *opt = data;
	unsigned bit;

	if (node->type == OF_IR_NODE_LIST)
		return;

	_debug_printf("%*s# vars_defined:", level, "");
	OF_BITMAP_FOR_EACH_SET_BIT(bit, node->ssa.vars_defined,
				   opt->num_vars) {
		_debug_printf(" @%d", bit);
	}
	_debug_printf("\n");

	if (node->ssa.depart_count)
		_debug_printf("%*s# depart_count: %d\n",
				level, "", node->ssa.depart_count);
	if (node->ssa.repeat_count)
		_debug_printf("%*s# repeat_count: %d\n",
				level, "", node->ssa.repeat_count);
	if (node->type == OF_IR_NODE_DEPART)
		_debug_printf("%*s# depart_number: %d\n",
				level, "", node->ssa.depart_number);
	if (node->type == OF_IR_NODE_REPEAT)
		_debug_printf("%*s# repeat_number: %d\n",
				level, "", node->ssa.repeat_number);
	if (!LIST_IS_EMPTY(&node->ssa.loop_phis)) {
		_debug_printf("%*s# loop_phis:\n", level + 4, "");
		dump_phis(&node->ssa.loop_phis, node->ssa.repeat_count + 1,
				level + 4);
	}
}

static void
dump_ssa_data_post(struct of_ir_shader *shader, struct of_ir_ast_node *node,
		   unsigned level, void *data)
{
	if (!LIST_IS_EMPTY(&node->ssa.phis)) {
		_debug_printf("%*s# phis:\n", level + 4, "");
		dump_phis(&node->ssa.phis, node->ssa.depart_count,
				level + 4);
	}
}

static void
dump_ssa_data(struct of_ir_shader *shader, struct of_ir_ast_node *node,
	      unsigned level, bool post, void *data)
{
	if (post)
		dump_ssa_data_post(shader, node, level, data);
	else
		dump_ssa_data_pre(shader, node, level, data);
}

/*
 * SSA conversion entry point.
 */

int
of_ir_to_ssa(struct of_ir_shader *shader)
{
	struct of_ir_optimizer *opt;
	struct of_heap *heap;

	heap = of_heap_create();
	opt = of_heap_alloc(heap, sizeof(*opt));
	opt->heap = heap;
	opt->shader = shader;
	opt->num_vars = shader->stats.num_vars;
	opt->vars_bitmap_size = OF_BITMAP_WORDS_FOR_BITS(opt->num_vars)
				* sizeof(uint32_t);
	opt->renames_stack = of_stack_create(opt->num_vars
						* sizeof(*opt->renames), 16);
	opt->renames = of_stack_top(opt->renames_stack);
	memset(opt->renames, 0, opt->num_vars * sizeof(*opt->renames));
	opt->shader_heap = shader->heap;
	opt->last_var = 1;

	RUN_PASS(shader, opt, init_nodes);
	RUN_PASS(shader, opt, variables_defined);
	RUN_PASS(shader, opt, dep_rep_count);
	RUN_PASS(shader, opt, insert_phi);
	RUN_PASS(shader, opt, make_ssa);

	shader->stats.num_vars = opt->last_var;

	OF_IR_DUMP_AST(shader, dump_ssa_data, opt, "post-ssa");

	of_stack_destroy(opt->renames_stack);
	of_heap_destroy(heap);

	return 0;
}
