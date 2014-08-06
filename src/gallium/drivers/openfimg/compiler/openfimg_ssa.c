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

struct of_ir_ssa {
	struct of_ir_shader *shader;
	unsigned vars_bitmap_size;
	unsigned vars_bitmap_bits;
	struct of_heap *heap;
};

static void
variables_defined_list(struct of_ir_ssa *ssa, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;

	LIST_FOR_EACH_ENTRY(ins, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;

		if (!dst || dst->type != OF_IR_REG_R)
			continue;

		of_bitmap_set(node->ssa.vars_defined, dst->num);
	}
}

static void
variables_defined(struct of_ir_ssa *ssa, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child, *parent;
	uint32_t *vars_defined;

	vars_defined = of_heap_alloc(ssa->heap, ssa->vars_bitmap_size);
	node->ssa.vars_defined = vars_defined;

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		variables_defined(ssa, child);

	switch (node->type) {
	case OF_IR_NODE_DEPART:
	case OF_IR_NODE_REPEAT:
		parent = node->depart_repeat.region;
		break;
	case OF_IR_NODE_LIST:
		variables_defined_list(ssa, node);
		/* Intentional fall-through. */
	case OF_IR_NODE_REGION:
		parent = node->parent;
		break;
	default:
		return;
	}

	if (parent)
		of_bitmap_or(parent->ssa.vars_defined, parent->ssa.vars_defined,
				vars_defined, ssa->num_vars);

	switch (node->type) {
	case OF_IR_NODE_DEPART:
	case OF_IR_NODE_REPEAT:
		memset(vars_defined, 0, ssa->vars_bitmap_size);
		break;
	default:
		break;
	}
}

static void
dep_rep_count(struct of_ir_ssa *ssa, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		dep_rep_count(ssa, child);

	switch (node->type) {
	case OF_IR_NODE_DEPART:
		node->ssa.depart_number =
				node->depart_repeat.region->ssa.depart_count++;
		break;
	case OF_IR_NODE_REPEAT:
		node->ssa.repeat_number =
				node->depart_repeat.region->ssa.repeat_count++;
		break;
	case OF_IR_NODE_IF_THEN:
		node->ssa.depart_count = 2; /* To make PHI insertion easier. */
		break;
	default:
		break;
	}
}

struct of_ir_phi {
	struct list_head list;
	unsigned dst;
	unsigned src[];
};

static void
make_trivials(struct of_ir_ssa *ssa, struct list_head *list, uint32_t *vars,
	      unsigned count)
{
	struct of_ir_phi *phi;
	unsigned bit;
	unsigned i;

	OF_BITMAP_FOR_EACH_SET_BIT(bit, vars, ssa->vars_bitmap_bits) {
		phi = of_heap_alloc(ssa->heap, sizeof(*phi)
					+ count * sizeof(*phi->src));
		phi->dst = bit;
		for (i = 0; i < count; ++i)
			phi->src[i] = bit;
		list_addtail(&phi->list, list);
	}
}

static void
insert_phi(struct of_ir_ssa *ssa, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		insert_phi(ssa, child);

	/* Handles if_then and region nodes. */
	if (node->ssa.depart_count > 1)
		make_trivials(ssa, &node->ssa.phis,
				node->ssa.vars_defined, node->ssa.depart_count);

	/* Handles region nodes with repeat subnodes. */
	if (node->ssa.repeat_count)
		make_trivials(ssa, &node->ssa.loop_phis,
				node->ssa.vars_defined,
				node->ssa.repeat_count + 1);
}

static void
init_nodes(struct of_ir_ssa *ssa, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	memset(&node->ssa, 0, sizeof(node->ssa));

	LIST_INITHEAD(&node->ssa.phis);
	LIST_INITHEAD(&node->ssa.loop_phis);

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		init_nodes(ssa, child);
}

static void
dump_phis(struct list_head *list, unsigned count, unsigned level)
{
	struct of_ir_phi *phi;
	unsigned i;

	LIST_FOR_EACH_ENTRY(phi, list, list) {
		_debug_printf("%*sR%d = PHI(R%d",
				level, "", phi->dst, phi->src[0]);
		for (i = 1; i < count; ++i)
			_debug_printf(", R%d", phi->src[i]);
		_debug_printf(")\n");
	}
}

static void
dump_ssa_data_pre(struct of_ir_shader *shader, struct of_ir_ast_node *node,
		  unsigned level, void *data)
{
	struct of_ir_ssa *ssa = data;
	unsigned bit;

	if (node->type == OF_IR_NODE_LIST)
		return;

	_debug_printf("%*s# vars_defined:", level, "");
	OF_BITMAP_FOR_EACH_SET_BIT(bit, node->ssa.vars_defined,
				   ssa->vars_bitmap_bits) {
		_debug_printf(" R%d", bit);
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

int
of_ir_to_ssa(struct of_ir_shader *shader)
{
	struct of_ir_ast_node *node;
	struct of_ir_ssa *ssa;
	struct of_heap *heap;

	heap = of_heap_create();
	ssa = of_heap_alloc(heap, sizeof(*ssa));
	ssa->heap = heap;
	ssa->shader = shader;
	ssa->vars_bitmap_bits = shader->stats.num_vars;
	ssa->vars_bitmap_size = OF_BITMAP_WORDS_FOR_BITS(ssa->vars_bitmap_bits)
				* sizeof(uint32_t);

	LIST_FOR_EACH_ENTRY(node, &shader->root_nodes, parent_list) {
		init_nodes(ssa, node);
		variables_defined(ssa, node);
		dep_rep_count(ssa, node);
		insert_phi(ssa, node);
	}

	of_ir_dump_ast(shader, dump_ssa_data, ssa);

	of_heap_destroy(heap);

	return 0;
}
