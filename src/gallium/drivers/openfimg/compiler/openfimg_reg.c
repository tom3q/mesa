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

struct of_ir_reg_assign {
	struct of_heap *heap;
	unsigned num_vars;
	uint32_t *live;
	uint32_t **interference;
	unsigned vars_bitmap_size;
};

static void
add_interference(struct of_ir_reg_assign *ra, uint16_t var1, uint16_t var2)
{
	if (!ra->interference[var1])
		ra->interference[var1] = of_heap_alloc(ra->heap,
							ra->vars_bitmap_size);

	if (!ra->interference[var2])
		ra->interference[var2] = of_heap_alloc(ra->heap,
							ra->vars_bitmap_size);

	of_bitmap_set(ra->interference[var1], var2);
	of_bitmap_set(ra->interference[var2], var1);
}

static void
interference_list(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins, *s;

	LIST_FOR_EACH_ENTRY_SAFE_REV(ins, s, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;
		unsigned comp;
		unsigned i;

		if (!dst || dst->type != OF_IR_REG_VAR)
			goto no_dst;

		for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
			if (!(dst->mask & (1 << comp)))
				continue;

			of_bitmap_clear(ra->live, dst->var[comp]);
		}

no_dst:
		for (i = 0; i < OF_IR_NUM_SRCS && ins->srcs[i]; ++i) {
			struct of_ir_register *src = ins->srcs[i];

			if (src->type != OF_IR_REG_VAR)
				continue;

			for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
				unsigned var;

				if (!(src->mask & (1 << comp)))
					continue;

				OF_BITMAP_FOR_EACH_SET_BIT(var, ra->live,
							   ra->num_vars)
					add_interference(ra, src->var[comp],
								var);

				of_bitmap_set(ra->live, src->var[comp]);
			}
		}
	}
}

static void
interference_phi(struct of_ir_reg_assign *ra, struct list_head *phis,
		 unsigned count)
{
	struct of_ir_phi *phi, *s;

	LIST_FOR_EACH_ENTRY_SAFE_REV(phi, s, phis, list) {
		unsigned i;

		of_bitmap_clear(ra->live, phi->dst);

		for (i = 0; i < count; ++i) {
			unsigned var;

			OF_BITMAP_FOR_EACH_SET_BIT(var, ra->live, ra->num_vars)
				add_interference(ra, phi->src[i], var);

			of_bitmap_set(ra->live, phi->src[i]);
		}
	}
}

static void
interference(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child, *s;

	if (node->type == OF_IR_NODE_LIST)
		return interference_list(ra, node);

	interference_phi(ra, &node->ssa.phis, node->ssa.depart_count);

	LIST_FOR_EACH_ENTRY_SAFE_REV(child, s, &node->nodes, parent_list)
		interference(ra, child);

	interference_phi(ra, &node->ssa.loop_phis, node->ssa.repeat_count + 1);
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
dump_ra_data_pre(struct of_ir_shader *shader, struct of_ir_ast_node *node,
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
dump_ra_data_post(struct of_ir_shader *shader, struct of_ir_ast_node *node,
		   unsigned level, void *data)
{
	if (!LIST_IS_EMPTY(&node->ssa.phis)) {
		_debug_printf("%*s# phis:\n", level + 4, "");
		dump_phis(&node->ssa.phis, node->ssa.depart_count,
				level + 4);
	}
}

static void
dump_ra_data(struct of_ir_shader *shader, struct of_ir_ast_node *node,
	      unsigned level, bool post, void *data)
{
	if (post)
		dump_ra_data_post(shader, node, level, data);
	else
		dump_ra_data_pre(shader, node, level, data);
}

static void
dump_interference(struct of_ir_reg_assign *ra)
{
	unsigned var1, var2;

	for (var1 = 0; var1 < ra->num_vars; ++var1) {
		if (!ra->interference[var1])
			continue;

		_debug_printf("@%d: ", var1);
		OF_BITMAP_FOR_EACH_SET_BIT(var2, ra->interference[var1],
					   ra->num_vars)
			_debug_printf("@%d ", var2);
		_debug_printf("\n");
	}
}

int
of_ir_assign_registers(struct of_ir_shader *shader)
{
	struct of_ir_ast_node *node;
	struct of_ir_reg_assign *ra;
	struct of_heap *heap;

	heap = of_heap_create();
	ra = of_heap_alloc(heap, sizeof(*ra));
	ra->heap = heap;
	ra->num_vars = shader->stats.num_vars;
	ra->vars_bitmap_size = OF_BITMAP_WORDS_FOR_BITS(ra->num_vars)
				* sizeof(uint32_t);
	ra->live = of_heap_alloc(heap, ra->vars_bitmap_size);
	ra->interference = of_heap_alloc(heap, ra->num_vars
						* sizeof(*ra->interference));

	LIST_FOR_EACH_ENTRY(node, &shader->root_nodes, parent_list) {
		interference(ra, node);
	}

	dump_interference(ra);

	DBG("AST (post-register-assignment/pre-CF-insertion)");
	of_ir_dump_ast(shader, dump_ra_data, ra);

	of_heap_destroy(heap);

	return 0;
}
