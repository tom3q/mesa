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

#include <util/u_dynarray.h>

#include "openfimg_ir_priv.h"
#include "openfimg_util.h"

enum of_ir_constraint_type {
	OF_IR_CONSTR_SAME_REG,
	OF_IR_CONSTR_PHI,
};

struct of_ir_chunk {
	struct list_head list;
	struct list_head vars;
	unsigned cost;
	uint8_t comp;
};

struct of_ir_variable {
	struct of_ir_chunk *chunk;
	struct list_head chunk_list;
	struct of_ir_instruction *def_ins;
	struct of_ir_phi *def_phi;
	uint32_t *interference;
	uint8_t comp;
};

struct of_ir_affinity {
	uint16_t vars[2];
	unsigned cost;
};

struct of_ir_constraint {
	uint16_t vars[OF_IR_VEC_SIZE];
	unsigned num_vars;
	enum of_ir_constraint_type type;
	struct list_head list;
	struct list_head next;
};

struct of_ir_reg_assign {
	struct of_ir_shader *shader;
	struct of_heap *heap;
	struct of_ir_variable *vars;
	uint32_t *live;
	unsigned num_vars;
	unsigned vars_bitmap_size;
	struct list_head chunks;
	struct list_head constraints;
	struct util_dynarray affinities;
	unsigned num_affinities;
};

/*
 * Interference graph construction.
 */

static void
add_interference(struct of_ir_reg_assign *ra, uint16_t var1, uint16_t var2)
{
	struct of_ir_variable *v1 = &ra->vars[var1];
	struct of_ir_variable *v2 = &ra->vars[var2];

	if (!v1->interference)
		v1->interference = of_heap_alloc(ra->heap,
							ra->vars_bitmap_size);

	if (!v2->interference)
		v2->interference = of_heap_alloc(ra->heap,
							ra->vars_bitmap_size);

	of_bitmap_set(v1->interference, var2);
	of_bitmap_set(v2->interference, var1);
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
			uint16_t var = dst->var[comp];

			if (!(dst->mask & (1 << comp)))
				continue;

			ra->vars[var].def_ins = ins;
			ra->vars[var].comp = comp + 1;
			of_bitmap_clear(ra->live, var);
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
		unsigned var = phi->dst;
		unsigned i;

		ra->vars[var].def_phi = phi;
		of_bitmap_clear(ra->live, var);

		for (i = 0; i < count; ++i) {
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

/*
 * Variable coalescing
 */

static int
affinity_compare(const void *a, const void *b)
{
	const struct of_ir_affinity *aa = *(const struct of_ir_affinity **)a;
	const struct of_ir_affinity *ab = *(const struct of_ir_affinity **)b;

	return aa->cost - ab->cost;
}

static struct of_ir_chunk *
create_chunk(struct of_ir_reg_assign *ra, struct of_ir_variable *var)
{
	struct of_ir_chunk *c = CALLOC_STRUCT(of_ir_chunk);

	LIST_INITHEAD(&c->vars);
	list_addtail(&var->chunk_list, &c->vars);
	list_addtail(&c->list, &ra->chunks);

	c->comp = var->comp;
	var->chunk = c;

	return c;
}

static void
destroy_chunk(struct of_ir_reg_assign *ra, struct of_ir_chunk *c)
{
	list_del(&c->list);
	FREE(c);
}

static INLINE uint16_t
var_num(struct of_ir_reg_assign *ra, struct of_ir_variable *var)
{
	return var - ra->vars;
}

static void
try_to_merge_chunks(struct of_ir_reg_assign *ra, struct of_ir_affinity *a,
		    struct of_ir_chunk *c0, struct of_ir_chunk *c1)
{
	struct of_ir_variable *v0, *v1;

	if (c0->comp && c1->comp && c0->comp != c1->comp)
		return;

	LIST_FOR_EACH_ENTRY(v0, &c0->vars, chunk_list) {
		LIST_FOR_EACH_ENTRY(v1, &c1->vars, chunk_list) {
			if (v1 == v0)
				continue;
			if (of_bitmap_get(v0->interference, var_num(ra, v1)))
				return;
		}
	}

	c0->comp |= c1->comp;
	c0->cost += a->cost;

	LIST_FOR_EACH_ENTRY(v1, &c1->vars, chunk_list)
		v1->chunk = c0;

	list_splice(&c1->vars, &c0->vars);
	destroy_chunk(ra, c1);
}

static void
prepare_chunks(struct of_ir_reg_assign *ra)
{
	struct of_ir_affinity **array = util_dynarray_begin(&ra->affinities);
	unsigned i;

	qsort(array, ra->num_affinities, sizeof(struct of_ir_affinity *),
		affinity_compare);

	for (i = 0; i < ra->num_affinities; ++i) {
		struct of_ir_affinity *a = array[i];
		struct of_ir_variable *v0 = &ra->vars[a->vars[0]];
		struct of_ir_variable *v1 = &ra->vars[a->vars[1]];

		if (!v0->chunk)
			create_chunk(ra, v0);

		if (!v1->chunk)
			create_chunk(ra, v1);

		if (v0->chunk == v1->chunk) {
			v1->chunk->cost += a->cost;
			continue;
		}

		try_to_merge_chunks(ra, a, v0->chunk, v1->chunk);
	}
}

static void
dump_chunks(struct of_ir_reg_assign *ra)
{
	struct of_ir_chunk *c;

	_debug_printf("Coalescer chunks:\n");

	LIST_FOR_EACH_ENTRY(c, &ra->chunks, list) {
		struct of_ir_variable *v;

		_debug_printf("{cost = %u, comp = %u}: ", c->cost, c->comp);

		LIST_FOR_EACH_ENTRY(v, &c->vars, chunk_list)
			_debug_printf("%u ", var_num(ra, v));

		_debug_printf("\n");
	}
}

static void
prepare_constraints(struct of_ir_reg_assign *ra)
{

}

static void
prepare_coalesce(struct of_ir_reg_assign *ra)
{
	prepare_chunks(ra);
	dump_chunks(ra);
	prepare_constraints(ra);
}

static struct of_ir_constraint *
create_constraint(struct of_ir_reg_assign *ra, enum of_ir_constraint_type type)
{
	struct of_ir_constraint *c = of_heap_alloc(ra->heap, sizeof(*c));

	c->type = type;
	list_addtail(&c->list, &ra->constraints);
	LIST_INITHEAD(&c->next);

	return c;
}

static INLINE void constraint_add_var(struct of_ir_reg_assign *ra,
				      struct of_ir_constraint *c, uint16_t var)
{
	struct of_ir_constraint *last = c;

	if (!LIST_IS_EMPTY(&c->next))
		last = LIST_ENTRY(struct of_ir_constraint, c->next.prev, list);

	if (last->num_vars == ARRAY_SIZE(last->vars)) {
		struct of_ir_constraint *new;

		new = of_heap_alloc(ra->heap, sizeof(*new));
		list_addtail(&new->list, &c->next);
		last = new;
	}

	last->vars[last->num_vars++] = var;
}

static void
add_affinity(struct of_ir_reg_assign *ra, uint16_t var1, uint16_t var2,
	     unsigned cost)
{
	struct of_ir_affinity *a = of_heap_alloc(ra->heap, sizeof(*a));

	a->vars[0] = var1;
	a->vars[1] = var2;
	a->cost = cost;

	util_dynarray_append(&ra->affinities, struct of_ir_affinity *, a);
	++ra->num_affinities;
}

/*
 * Live range splitting.
 */

static struct of_ir_instruction *
create_copy(struct of_ir_shader *shader, uint16_t dst_var, uint16_t src_var)
{
	struct of_ir_register *dst, *src;
	struct of_ir_instruction *ins;

	ins = of_ir_instr_create(shader, OF_OP_MOV);

	dst = of_ir_reg_create(shader, OF_IR_REG_VAR, 0, "x___", 0);
	of_ir_instr_add_dst(ins, dst);
	dst->var[0] = dst_var;
	dst->mask = 1;

	src = of_ir_reg_create(shader, OF_IR_REG_VAR, 0, "xxxx", 0);
	of_ir_instr_add_src(ins, src);
	src->var[0] = src_var;
	src->mask = 1;

	return ins;
}

static INLINE bool
is_vector(struct of_ir_register *reg)
{
	return reg->mask & (reg->mask - 1);
}

static INLINE bool
comp_used(struct of_ir_register *reg, unsigned comp)
{
	return reg->mask & (1 << comp);
}

static void
split_operand(struct of_ir_reg_assign *ra, struct of_ir_instruction *ins,
	      struct of_ir_register *reg, bool dst)
{
	struct of_ir_constraint *c;
	unsigned comp;

	c = create_constraint(ra, OF_IR_CONSTR_SAME_REG);

	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
		struct of_ir_instruction *copy;

		if (!comp_used(reg, comp))
			continue;

		constraint_add_var(ra, c, reg->var[comp]);

		if (dst) {
			copy = create_copy(ra->shader, reg->var[comp],
						ra->num_vars);
			of_ir_instr_insert(ra->shader, NULL, ins, copy);
		} else {
			copy = create_copy(ra->shader, ra->num_vars,
						reg->var[comp]);
			of_ir_instr_insert_before(ra->shader,
							NULL, ins, copy);
		}

		add_affinity(ra, ra->num_vars, reg->var[comp], 20000);
		reg->var[comp] = ra->num_vars++;
	}
}

static void
split_live_list(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins, *s;

	LIST_FOR_EACH_ENTRY_SAFE_REV(ins, s, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;
		unsigned i;

		if (dst && dst->type == OF_IR_REG_VAR && is_vector(dst))
			split_operand(ra, ins, dst, true);

		for (i = 0; i < OF_IR_NUM_SRCS && ins->srcs[i]; ++i) {
			struct of_ir_register *src = ins->srcs[i];

			if (src->type == OF_IR_REG_VAR && is_vector(src))
				split_operand(ra, ins, src, false);
		}
	}
}

static void
split_live_phi_src(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node,
		   struct list_head *phis, unsigned arg, bool loop)
{
	struct of_ir_ast_node *list = NULL;
	struct of_ir_phi *phi;

	LIST_FOR_EACH_ENTRY(phi, phis, list) {
		struct of_ir_instruction *ins;
		uint16_t tmp;

		if (!phi->src[arg])
			continue;

		if (!list) {
			if (loop && !arg)
				list = of_ir_node_list_before(node);
			else
				list = of_ir_node_list_back(node);

			if (!list)
				return;
		}

		tmp = ra->num_vars++;
		ins = create_copy(node->shader, tmp, phi->src[arg]);
		of_ir_instr_insert(node->shader, list, NULL, ins);

		add_affinity(ra, tmp, phi->src[arg], 1);
		add_affinity(ra, tmp, phi->dst, 10000);

		phi->src[arg] = tmp;
	}
}

static void
split_live_phi_dst(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node,
		   struct list_head *phis, bool loop)
{
	struct of_ir_ast_node *list = NULL;
	struct of_ir_phi *phi;

	LIST_FOR_EACH_ENTRY(phi, phis, list) {
		struct of_ir_instruction *ins;
		uint16_t tmp;

		if (!list) {
			if (loop)
				list = of_ir_node_list_front(node);
			else
				list = of_ir_node_list_after(node);

			if (!list)
				return;
		}

		tmp = ra->num_vars++;
		ins = create_copy(node->shader, phi->dst, tmp);
		of_ir_instr_insert_before(node->shader, list, NULL, ins);

		add_affinity(ra, tmp, phi->dst, 1);

		phi->dst = tmp;
	}
}

static void
constraint_phi(struct of_ir_reg_assign *ra, struct list_head *phis,
	       unsigned num_srcs)
{
	struct of_ir_phi *phi;

	LIST_FOR_EACH_ENTRY(phi, phis, list) {
		struct of_ir_constraint *c;
		unsigned i;

		c = create_constraint(ra, OF_IR_CONSTR_PHI);
		constraint_add_var(ra, c, phi->dst);

		for (i = 0; i < num_srcs; ++i)
			constraint_add_var(ra, c, phi->src[i]);
	}
}

static void
split_live(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child, *s;
	struct of_ir_ast_node *region;

	switch (node->type) {
	case OF_IR_NODE_LIST:
		return split_live_list(ra, node);

	case OF_IR_NODE_DEPART:
		region = node->depart_repeat.region;
		split_live_phi_src(ra, node, &region->ssa.phis,
					node->ssa.depart_number, false);
		break;

	case OF_IR_NODE_REPEAT:
		region = node->depart_repeat.region;
		split_live_phi_src(ra, node, &region->ssa.loop_phis,
					node->ssa.repeat_number + 1, true);
		break;

	case OF_IR_NODE_REGION:
		split_live_phi_dst(ra, node, &node->ssa.phis, false);
		split_live_phi_dst(ra, node, &node->ssa.loop_phis, true);
		split_live_phi_src(ra, node, &node->ssa.loop_phis, 0, true);
		break;

	default:
		break;
	}

	LIST_FOR_EACH_ENTRY_SAFE_REV(child, s, &node->nodes, parent_list)
		split_live(ra, child);

	if (node->type == OF_IR_NODE_REGION) {
		constraint_phi(ra, &node->ssa.phis, node->ssa.depart_count);
		constraint_phi(ra, &node->ssa.loop_phis,
				node->ssa.repeat_count + 1);
	}
}

/*
 * Constrained instruction precoloring.
 */

static void
precolor_constrained(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node)
{

}

/*
 * AST dumping.
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
		if (!ra->vars[var1].interference)
			continue;

		_debug_printf("@%d: ", var1);
		OF_BITMAP_FOR_EACH_SET_BIT(var2, ra->vars[var1].interference,
					   ra->num_vars)
			_debug_printf("@%d ", var2);
		_debug_printf("\n");
	}
}

int
of_ir_assign_registers(struct of_ir_shader *shader)
{
	struct of_ir_reg_assign *ra;
	struct of_heap *heap;

	heap = of_heap_create();
	ra = of_heap_alloc(heap, sizeof(*ra));
	ra->shader = shader;
	ra->heap = heap;
	ra->num_vars = shader->stats.num_vars;
	LIST_INITHEAD(&ra->constraints);
	LIST_INITHEAD(&ra->chunks);
	util_dynarray_init(&ra->affinities);

	RUN_PASS(shader, ra, split_live);

	ra->vars_bitmap_size = OF_BITMAP_WORDS_FOR_BITS(ra->num_vars)
				* sizeof(uint32_t);
	ra->live = CALLOC(1, ra->vars_bitmap_size);
	ra->vars = CALLOC(ra->num_vars, sizeof(*ra->vars));

	RUN_PASS(shader, ra, interference);
	dump_interference(ra);

	FREE(ra->live);

	prepare_coalesce(ra);
	RUN_PASS(shader, ra, precolor_constrained);

	DBG("AST (post-register-assignment/pre-CF-insertion)");
	of_ir_dump_ast(shader, dump_ra_data, ra);

	FREE(ra->vars);
	of_heap_destroy(heap);

	return 0;
}
