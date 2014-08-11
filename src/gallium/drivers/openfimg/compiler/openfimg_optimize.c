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

#include <util/u_hash_table.h>

#include "openfimg_ir_priv.h"
#include "openfimg_util.h"

struct of_ir_optimize {
	struct of_heap *heap;
	unsigned *ref_counts;
	unsigned num_vars;
};

static int
eliminate_dead_list(struct of_ir_optimize *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins, *s;
	int ret = 0;

	LIST_FOR_EACH_ENTRY_SAFE_REV(ins, s, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;
		const struct of_ir_opc_info *info;
		unsigned dcomp;

		info = of_ir_get_opc_info(ins->opc);

		for (dcomp = 0; dcomp < OF_IR_VEC_SIZE; ++dcomp) {
			unsigned i;

			if (!dst || dst->type != OF_IR_REG_VAR)
				goto has_ref;

			if (!(dst->mask & (1 << dcomp)))
				continue;

			if (!opt->ref_counts[dst->var[dcomp]]) {
				dst->mask &= ~(1 << dcomp);
				ret = 1;
				continue;
			}

has_ref:
			for (i = 0; i < OF_IR_NUM_SRCS && ins->srcs[i]; ++i) {
				struct of_ir_register *src = ins->srcs[i];
				const dst_map_t *dst_map = &info->dst_map[i];
				unsigned scomp;

				if (src->type != OF_IR_REG_VAR)
					continue;

				for (scomp = 0; scomp < OF_IR_VEC_SIZE; ++scomp) {
					const char *mask = (*dst_map)[dcomp];

					if (!(src->mask & (1 << scomp)))
						continue;

					if (mask[scomp] != "xyzw"[scomp])
						continue;

					++opt->ref_counts[src->var[scomp]];
				}
			}
		}

		if (!dst->mask)
			list_del(&ins->list);
	}

	return ret;
}

static int
eliminate_dead_phi(struct of_ir_optimize *opt, struct list_head *phis,
		   unsigned count)
{
	struct of_ir_phi *phi, *s;
	int ret = 0;

	LIST_FOR_EACH_ENTRY_SAFE_REV(phi, s, phis, list) {
		unsigned i;

		if (opt->ref_counts[phi->dst])
			continue;

		for (i = 0; i < count; ++i)
			--opt->ref_counts[phi->src[i]];

		list_del(&phi->list);
		ret = 1;
	}

	return ret;
}

static void
assess_phi(struct of_ir_optimize *opt, struct list_head *phis,
		   unsigned count)
{
	struct of_ir_phi *phi, *s;

	LIST_FOR_EACH_ENTRY_SAFE_REV(phi, s, phis, list) {
		unsigned i;

		for (i = 0; i < count; ++i)
			++opt->ref_counts[phi->src[i]];
	}
}

static int
eliminate_dead_pass(struct of_ir_optimize *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child, *s;
	int ret = 0;

	if (node->type == OF_IR_NODE_LIST)
		return eliminate_dead_list(opt, node);

	assess_phi(opt, &node->ssa.phis, node->ssa.depart_count);
	ret |= eliminate_dead_phi(opt, &node->ssa.phis, node->ssa.depart_count);

	assess_phi(opt, &node->ssa.loop_phis, node->ssa.repeat_count + 1);

	LIST_FOR_EACH_ENTRY_SAFE_REV(child, s, &node->nodes, parent_list)
		ret |= eliminate_dead_pass(opt, child);

	ret |= eliminate_dead_phi(opt, &node->ssa.loop_phis,
					node->ssa.repeat_count + 1);

	return ret;
}

static void
eliminate_dead(struct of_ir_optimize *opt, struct of_ir_ast_node *node)
{
	unsigned pass = 0;
	int ret;

	do {
		memset(opt->ref_counts, 0, opt->num_vars
			* sizeof(*opt->ref_counts));
		DBG("Dead code elimination, pass %d", ++pass);
		ret = eliminate_dead_pass(opt, node);
	} while (ret);
}

static void
clean_sources_list(struct of_ir_optimize *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;

	LIST_FOR_EACH_ENTRY(ins, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;
		const struct of_ir_opc_info *info;
		unsigned i;

		info = of_ir_get_opc_info(ins->opc);

		for (i = 0; i < OF_IR_NUM_SRCS && ins->srcs[i]; ++i) {
			const dst_map_t *dst_map = &info->dst_map[i];
			struct of_ir_register *src = ins->srcs[i];
			unsigned src_mask = 0;
			unsigned dcomp;

			for (dcomp = 0; dcomp < OF_IR_VEC_SIZE; ++dcomp) {
				const char *mask = (*dst_map)[dcomp];
				unsigned scomp;

				if (!(dst->mask & (1 << dcomp)))
					continue;

				for (scomp = 0; scomp < OF_IR_VEC_SIZE; ++scomp) {
					if (mask[scomp] == "xyzw"[scomp])
						src_mask |= (1 << scomp);
				}
			}

			src->mask = src_mask;
		}
	}
}

static void
clean_sources(struct of_ir_optimize *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	if (node->type == OF_IR_NODE_LIST)
		return clean_sources_list(opt, node);

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		clean_sources(opt, child);
}

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
dump_opt_data_pre(struct of_ir_shader *shader, struct of_ir_ast_node *node,
		  unsigned level, void *data)
{
	if (node->type == OF_IR_NODE_LIST)
		return;

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
dump_opt_data_post(struct of_ir_shader *shader, struct of_ir_ast_node *node,
		   unsigned level, void *data)
{
	if (!LIST_IS_EMPTY(&node->ssa.phis)) {
		_debug_printf("%*s# phis:\n", level + 4, "");
		dump_phis(&node->ssa.phis, node->ssa.depart_count,
				level + 4);
	}
}

static void
dump_opt_data(struct of_ir_shader *shader, struct of_ir_ast_node *node,
	      unsigned level, bool post, void *data)
{
	if (post)
		dump_opt_data_post(shader, node, level, data);
	else
		dump_opt_data_pre(shader, node, level, data);
}

int
of_ir_optimize(struct of_ir_shader *shader)
{
	struct of_ir_optimize *opt;
	struct of_heap *heap;

	heap = of_heap_create();
	opt = of_heap_alloc(heap, sizeof(*opt));
	opt->heap = heap;
	opt->num_vars = shader->stats.num_vars;
	opt->ref_counts = of_heap_alloc(heap, opt->num_vars
					* sizeof(*opt->ref_counts));

	RUN_PASS(shader, opt, eliminate_dead);
	RUN_PASS(shader, opt, clean_sources);

	DBG("AST (post-optimize/pre-register-assignment)");
	of_ir_dump_ast(shader, dump_opt_data, opt);

	of_heap_destroy(heap);

	return 0;
}
