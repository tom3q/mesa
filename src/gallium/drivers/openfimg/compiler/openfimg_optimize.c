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
	struct util_hash_table *ref_ht;
};

static unsigned
hash_variable(void *key)
{
	return (unsigned)(unsigned long)key;
}

static int
compare_variable(void *key1, void *key2)
{
	unsigned var1 = (unsigned long)key1;
	unsigned var2 = (unsigned long)key2;

	return var1 - var2;
}

static void
eval_liveness_list(struct of_ir_optimize *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;

	LIST_FOR_EACH_ENTRY(ins, &node->list.instrs, list) {
		unsigned i;

		for (i = 0; i < OF_IR_NUM_SRCS && ins->srcs[i]; ++i) {
			struct of_ir_register *src = ins->srcs[i];
			unsigned comp;

			if (src->type != OF_IR_REG_VAR)
				continue;

			for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
				unsigned long var = src->var[comp];
				unsigned long ref_cnt;

				if (!(src->mask & (1 << comp)))
					continue;

				ref_cnt = (unsigned long)util_hash_table_get(
						opt->ref_ht, (void *)var);
				util_hash_table_set(opt->ref_ht, (void *)var,
						(void *)(ref_cnt + 1));
			}
		}
	}
}

static void
eval_liveness_phi(struct of_ir_optimize *opt, struct list_head *phis,
		  unsigned count)
{
	struct of_ir_phi *phi;

	LIST_FOR_EACH_ENTRY(phi, phis, list) {
		unsigned i;

		for (i = 0; i < count; ++i) {
			unsigned long var = phi->src[i];
			unsigned long ref_cnt;

			ref_cnt = (unsigned long)util_hash_table_get(
						opt->ref_ht, (void *)var);
			util_hash_table_set(opt->ref_ht, (void *)var,
						(void *)(ref_cnt + 1));
		}
	}
}

static void
eval_liveness(struct of_ir_optimize *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	if (node->type == OF_IR_NODE_LIST) {
		eval_liveness_list(opt, node);
		return;
	}

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		eval_liveness(opt, child);

	eval_liveness_phi(opt, &node->ssa.phis, node->ssa.depart_count);
	eval_liveness_phi(opt, &node->ssa.loop_phis,
				node->ssa.repeat_count + 1);
}

static int
eliminate_dead_list(struct of_ir_optimize *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins, *s;
	int ret = 0;

	LIST_FOR_EACH_ENTRY_SAFE_REV(ins, s, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;
		bool referenced = false;
		unsigned comp;
		unsigned i;

		if (!dst || dst->type != OF_IR_REG_VAR)
			continue;

		for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
			unsigned long var = dst->var[comp];

			if (!(dst->mask & (1 << comp)))
				continue;

			if (util_hash_table_get(opt->ref_ht, (void *)var)) {
				referenced = true;
				break;
			}
		}

		if (referenced)
			continue;

		for (i = 0; i < OF_IR_NUM_SRCS && ins->srcs[i]; ++i) {
			struct of_ir_register *src = ins->srcs[i];

			if (src->type != OF_IR_REG_VAR)
				continue;

			for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
				unsigned long ref_cnt;
				unsigned long var = src->var[comp];

				if (!(src->mask & (1 << comp)))
					continue;

				ref_cnt = (unsigned long)util_hash_table_get(
						opt->ref_ht, (void *)var);
				util_hash_table_set(opt->ref_ht, (void *)var,
						(void *)(ref_cnt - 1));
			}
		}

		ret = 1;
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
		unsigned long dst_var = phi->dst;
		unsigned i;

		if (util_hash_table_get(opt->ref_ht, (void *)dst_var))
			continue;

		for (i = 0; i < count; ++i) {
			unsigned long var = phi->src[i];
			unsigned long ref_cnt;

			ref_cnt = (unsigned long)util_hash_table_get(
						opt->ref_ht, (void *)var);
			util_hash_table_set(opt->ref_ht, (void *)var,
						(void *)(ref_cnt - 1));
		}

		ret = 1;
		list_del(&phi->list);
	}

	return ret;
}

static int
eliminate_dead_pass(struct of_ir_optimize *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child, *s;
	int ret = 0;

	if (node->type == OF_IR_NODE_LIST)
		return eliminate_dead_list(opt, node);

	ret |= eliminate_dead_phi(opt, &node->ssa.phis, node->ssa.depart_count);
	ret |= eliminate_dead_phi(opt, &node->ssa.loop_phis,
					node->ssa.repeat_count + 1);

	LIST_FOR_EACH_ENTRY_SAFE_REV(child, s, &node->nodes, parent_list)
		ret |= eliminate_dead_pass(opt, child);

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
	opt->ref_ht = util_hash_table_create(hash_variable, compare_variable);

	LIST_FOR_EACH_ENTRY(node, &shader->root_nodes, parent_list) {
		eval_liveness(opt, node);
		eliminate_dead(opt, node);
	}

	DBG("AST (post-optimize/pre-register-assignment)");
	of_ir_dump_ast(shader, NULL, NULL);

	of_heap_destroy(heap);

	return 0;
}
