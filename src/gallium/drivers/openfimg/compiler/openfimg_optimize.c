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
};

static int
eliminate_dead_list(struct of_ir_optimize *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins, *s;
	int ret = 0;

	LIST_FOR_EACH_ENTRY_SAFE_REV(ins, s, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;
		unsigned dcomp;

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
				unsigned scomp;

				if (src->type != OF_IR_REG_VAR)
					continue;

				for (scomp = 0; scomp < OF_IR_VEC_SIZE; ++scomp) {
					if (!(src->mask & (1 << scomp)))
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
		DBG("Dead code elimination, pass %d", ++pass);
		ret = eliminate_dead_pass(opt, node);
	} while (ret);
}

int
of_ir_optimize(struct of_ir_shader *shader)
{
	struct of_ir_ast_node *node;
	struct of_ir_optimize *opt;
	struct of_heap *heap;

	heap = of_heap_create();
	opt = of_heap_alloc(heap, sizeof(*opt));
	opt->heap = heap;
	opt->ref_counts = of_heap_alloc(heap, shader->stats.num_vars
					* sizeof(*opt->ref_counts));

	LIST_FOR_EACH_ENTRY(node, &shader->root_nodes, parent_list) {
		eliminate_dead(opt, node);
	}

	DBG("AST (post-optimize/pre-register-assignment)");
	of_ir_dump_ast(shader, NULL, NULL);

	of_heap_destroy(heap);

	return 0;
}
