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
#include <util/u_slab.h>

#include "openfimg_ir_priv.h"
#include "openfimg_util.h"

enum of_ir_constraint_type {
	OF_IR_CONSTR_SAME_REG = (1 << 0),
	OF_IR_CONSTR_PHI = (1 << 1),
};

struct of_ir_chunk {
	struct list_head list;
	struct of_valset vars;
	unsigned num_vars;
	unsigned cost;
	unsigned color;
	uint8_t comp;
	unsigned fixed :1;
	unsigned prealloc :1;
};

struct of_ir_variable {
	struct of_ir_chunk *chunk;
	struct of_ir_instruction *def_ins;
	struct of_ir_phi *def_phi;
	uint32_t *interference;
	unsigned constraints;
	unsigned color;
	uint8_t comp;
	unsigned fixed :1;
};

struct of_ir_affinity {
	uint16_t vars[2];
	unsigned cost;
};

struct of_ir_constraint {
	struct of_valset vars;
	unsigned num_vars;
	unsigned cost;
	enum of_ir_constraint_type type;
};

struct of_ir_reg_assign {
	struct of_ir_shader *shader;
	struct of_heap *heap;
	struct util_slab_mempool valset_slab;
	uint32_t *live;
	struct util_dynarray vars;
	unsigned num_vars;
	struct util_slab_mempool chunk_slab;
	struct list_head chunks;
	struct util_dynarray constraints;
	unsigned num_constraints;
	struct util_dynarray affinities;
	unsigned num_affinities;
	uint32_t *reg_bitmap[4];
	uint32_t *chunk_interf;
	struct util_dynarray chunk_queue;
	unsigned chunk_queue_len;
	struct of_stack *renames_stack;
	uint16_t *renames;
};

/*
 * Variable management.
 */

static INLINE uint16_t
var_num(struct of_ir_reg_assign *ra, struct of_ir_variable *var)
{
	struct of_ir_variable *vars = util_dynarray_begin(&ra->vars);
	return var - vars;
}

static INLINE struct of_ir_variable *
get_var(struct of_ir_reg_assign *ra, uint16_t var)
{
	assert(var < ra->num_vars);
	return util_dynarray_element(&ra->vars, struct of_ir_variable, var);
}

static INLINE struct of_ir_variable *
add_var(struct of_ir_reg_assign *ra)
{
	static const struct of_ir_variable v = { 0, };

	util_dynarray_append(&ra->vars, struct of_ir_variable, v);
	++ra->num_vars;
	return util_dynarray_top_ptr(&ra->vars, struct of_ir_variable);
}

static INLINE uint16_t
add_var_num(struct of_ir_reg_assign *ra)
{
	return var_num(ra, add_var(ra));
}

/*
 * Interference graph construction.
 */

static INLINE bool
vars_interference(struct of_ir_reg_assign *ra, uint16_t var1, uint16_t var2)
{
	struct of_ir_variable *v1 = get_var(ra, var1);

	assert(var1 < ra->num_vars);
	assert(var2 < ra->num_vars);

	return v1->interference && of_bitmap_get(v1->interference, var2);
}

static void
add_interference(struct of_ir_reg_assign *ra, uint16_t var1, uint16_t var2)
{
	struct of_ir_variable *v1 = get_var(ra, var1);
	struct of_ir_variable *v2 = get_var(ra, var2);

	assert(var1 < ra->num_vars);
	assert(var2 < ra->num_vars);

	if (!v1->interference)
		v1->interference = of_heap_alloc(ra->heap,
					OF_BITMAP_BYTES_FOR_BITS(ra->num_vars));

	if (!v2->interference)
		v2->interference = of_heap_alloc(ra->heap,
					OF_BITMAP_BYTES_FOR_BITS(ra->num_vars));

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

			get_var(ra, var)->def_ins = ins;
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

		get_var(ra, var)->def_phi = phi;
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

	return ab->cost - aa->cost;
}

static struct of_ir_chunk *
create_chunk(struct of_ir_reg_assign *ra, struct of_ir_variable *var)
{
	struct of_ir_chunk *c = util_slab_alloc(&ra->chunk_slab);
	uint16_t num = var_num(ra, var);

	memset(c, 0, sizeof(*c));

	list_addtail(&c->list, &ra->chunks);

	if (var->chunk) {
		of_valset_del(&var->chunk->vars, num);
		--var->chunk->num_vars;
	}

	of_valset_init(&c->vars, &ra->valset_slab);
	of_valset_add(&c->vars, num);

	c->num_vars = 1;
	c->comp = var->comp;
	var->chunk = c;

	return c;
}

static void
destroy_chunk(struct of_ir_reg_assign *ra, struct of_ir_chunk *c)
{
	list_del(&c->list);
	util_slab_free(&ra->chunk_slab, c);
}

static void
try_to_merge_chunks(struct of_ir_reg_assign *ra, struct of_ir_affinity *a,
		    struct of_ir_chunk *c0, struct of_ir_chunk *c1)
{
	unsigned long *num0, *num1;

	if (c0->comp && c1->comp && c0->comp != c1->comp)
		return;

	OF_VALSET_FOR_EACH_VAL(num0, &c0->vars) {
		OF_VALSET_FOR_EACH_VAL(num1, &c1->vars) {
			if (*num0 == *num1)
				continue;
			if (vars_interference(ra, *num0, *num1))
				return;
		}
	}

	c0->num_vars += c1->num_vars;
	c0->comp |= c1->comp;
	c0->cost += a->cost;

	OF_VALSET_FOR_EACH_VAL(num1, &c1->vars) {
		struct of_ir_variable *v1 = get_var(ra, *num1);

		of_valset_add(&c0->vars, *num1);
		v1->chunk = c0;
	}

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
		struct of_ir_variable *v0 = get_var(ra, a->vars[0]);
		struct of_ir_variable *v1 = get_var(ra, a->vars[1]);

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
		unsigned long *num;

		_debug_printf("{cost = %u, comp = %u}: ", c->cost, c->comp);

		OF_VALSET_FOR_EACH_VAL(num, &c->vars)
			_debug_printf("%lu ", *num);

		_debug_printf("\n");
	}
}

static int
constraint_compare(const void *a, const void *b)
{
	const struct of_ir_constraint *ca =
					*(const struct of_ir_constraint **)a;
	const struct of_ir_constraint *cb =
					*(const struct of_ir_constraint **)b;

	if (ca->type != cb->type)
		return ca->type - cb->type;

	return cb->cost - ca->cost;
}

static void
prepare_constraints(struct of_ir_reg_assign *ra)
{
	struct of_ir_constraint **array = util_dynarray_begin(&ra->constraints);
	unsigned i;

	for (i = 0; i < ra->num_constraints; ++i) {
		struct of_ir_constraint *c = array[i];
		unsigned long *num;

		if (c->type != OF_IR_CONSTR_SAME_REG)
			continue;

		OF_VALSET_FOR_EACH_VAL(num, &c->vars) {
			struct of_ir_variable *v = get_var(ra, *num);

			if (!v->chunk)
				create_chunk(ra, v);
			else
				c->cost += v->chunk->cost;
		}
	}

	qsort(array, ra->num_constraints, sizeof(*array), constraint_compare);
}

static void
dump_constraints(struct of_ir_reg_assign *ra)
{
	struct of_ir_constraint **array = util_dynarray_begin(&ra->constraints);
	unsigned i;

	_debug_printf("Coloring constraints:\n");

	for (i = 0; i < ra->num_constraints; ++i) {
		struct of_ir_constraint *c = array[i];
		unsigned long *num;

		_debug_printf("{type = %u, cost = %u}: ", c->type, c->cost);

		OF_VALSET_FOR_EACH_VAL(num, &c->vars)
			_debug_printf("%u ", *num);

		_debug_printf("\n");
	}
}

static bool
next_swizzle(uint8_t swz[4])
{
	unsigned i;
	int k = -1, l = -1;

	for (i = 0; i < 4 - 1; ++i) {
		if (swz[i] < swz[i + 1]) {
			k = i;
			l = i + 1;
		}
	}

	if (k < 0)
		return false;

	for (i = l + 1; i < 4; ++i)
		if (swz[k] < swz[i])
			l = i;

	swap(swz[k], swz[l], uint8_t);

	for (i = 0; i < (4 - (k + 1)) / 2; ++i)
		swap(swz[k + 1 + i], swz[4 - 1 - i], uint8_t);

	return true;
}

#define OF_NUM_REGS		32
#define OF_REG_BITMAP_BITS	(OF_NUM_REGS * OF_IR_VEC_SIZE + 1)
#define OF_REG_BITMAP_SIZE	(OF_BITMAP_WORDS_FOR_BITS(OF_REG_BITMAP_BITS))

static INLINE unsigned
make_color(uint16_t reg, uint8_t swz)
{
	return reg * OF_IR_VEC_SIZE + swz + 1;
}

static INLINE uint8_t
color_comp(unsigned color)
{
	return (color - 1) % OF_IR_VEC_SIZE;
}

static INLINE uint8_t
color_reg(unsigned color)
{
	return (color - 1) / OF_IR_VEC_SIZE;
}

static void
color_chunk(struct of_ir_reg_assign *ra, struct of_ir_chunk *c, unsigned color)
{
	uint8_t comp = color_comp(color);
	unsigned long *num;

	OF_VALSET_FOR_EACH_VAL(num, &c->vars) {
		struct of_ir_variable *v = get_var(ra, *num);

		if (v->comp && v->comp != color_comp(color)) {
			create_chunk(ra, v);
			continue;
		}

		v->color = color;

		if (v->constraints & OF_IR_CONSTR_PHI) {
			v->fixed = 1;
			c->fixed = 1;
		}

		DBG("assigned R%u.%c to @%u",
			color_reg(color), "xyzw"[comp], *num);
	}

	c->color = color;
}

static void
init_reg_bitmap(struct of_ir_reg_assign *ra, uint32_t **regs,
		uint32_t *interf)
{
	struct of_ir_variable *v;
	uint32_t *bitmap;
	unsigned var;

	bitmap = *regs;
	if (!bitmap)
		*regs = bitmap = of_heap_alloc(ra->heap, OF_REG_BITMAP_SIZE);

	memset(bitmap, 0xff, OF_REG_BITMAP_SIZE);
	of_bitmap_clear(bitmap, 0);

	if (!interf)
		return;

	/* Mark registers already assigned to interfering variables. */
	OF_BITMAP_FOR_EACH_SET_BIT(var, interf, ra->num_vars) {
		v = get_var(ra, var);
		if (v->color)
			of_bitmap_clear(bitmap, v->color);
	}
}

static void
init_reg_bitmap_for_chunk(struct of_ir_reg_assign *ra, uint32_t **regs,
			  struct of_ir_chunk *c)
{
	struct of_ir_variable *v;
	unsigned long *num;
	uint32_t *interf;

	interf = ra->chunk_interf;
	if (!interf)
		ra->chunk_interf = interf = of_heap_alloc(ra->heap,
					OF_BITMAP_BYTES_FOR_BITS(ra->num_vars));
	memset(interf, 0, OF_BITMAP_BYTES_FOR_BITS(ra->num_vars));

	/* Calculate interference of the chunk. */
	OF_VALSET_FOR_EACH_VAL(num, &c->vars) {
		v = get_var(ra, *num);
		if (!v->interference)
			continue;
		of_bitmap_or(interf, interf, v->interference, ra->num_vars);
	}

	OF_VALSET_FOR_EACH_VAL(num, &c->vars)
		of_bitmap_clear(interf, *num);

	init_reg_bitmap(ra, regs, interf);
}

static int
color_reg_constraint(struct of_ir_reg_assign *ra, struct of_ir_constraint *c)
{
	struct of_ir_chunk *ch[OF_IR_VEC_SIZE];
	unsigned comp_mask = 0;
	uint8_t swz[OF_IR_VEC_SIZE] = {0, 1, 2, 3};
	unsigned long *num;
	unsigned num_vars;
	unsigned reg;
	unsigned i;

	i = 0;
	OF_VALSET_FOR_EACH_VAL(num, &c->vars) {
		struct of_ir_variable *v = get_var(ra, *num);

		if (!v->chunk)
			create_chunk(ra, v);

		ch[i] = v->chunk;

		if (v->chunk->comp) {
			if (v->chunk->comp & comp_mask) {
				ch[i] = create_chunk(ra, v);
				assert(!ch[i]->comp);
			} else {
				comp_mask |= v->chunk->comp;
			}
		}

		init_reg_bitmap_for_chunk(ra, &ra->reg_bitmap[i], ch[i]);
		++i;
	}
	num_vars = i;

	do {
		for (i = 0; i < num_vars; ++i)
			if (ch[i]->comp && ch[i]->comp != (1 << swz[i]))
				break;
		if (i != num_vars)
			continue;

		for (reg = 0; reg < OF_NUM_REGS - 1; ++reg) {
			for (i = 0; i < num_vars; ++i) {
				unsigned color = make_color(reg, swz[i]);

				if (!of_bitmap_get(ra->reg_bitmap[i], color))
					break;
			}
			if (i == num_vars)
				goto done;
		}
	} while (next_swizzle(swz));

	DBG("out of registers");
	return -1;

done:
	DBG("reg = %u, swz = (%d,%d,%d,%d)",
		reg, swz[0], swz[1], swz[2], swz[3]);

	i = 0;
	OF_VALSET_FOR_EACH_VAL(num, &c->vars) {
		struct of_ir_variable *v = get_var(ra, *num);
		unsigned color = make_color(reg, swz[i]);
		struct of_ir_chunk *cc = ch[i];

		if (v->color) {
			/* FIXME: Do not allow duplicates in constraints. */
			++i;
			continue;
		}

		if (cc->fixed) {
			if (cc->color == color) {
				++i;
				continue;
			}
			cc = create_chunk(ra, v);
		}

		color_chunk(ra, cc, color);
		cc->fixed = 1;
		cc->prealloc = 1;
		++i;
	}

	return 0;
}

static int
color_constraints(struct of_ir_reg_assign *ra)
{
	struct of_ir_constraint **array = util_dynarray_begin(&ra->constraints);
	unsigned i;

	for (i = 0; i < ra->num_constraints; ++i) {
		struct of_ir_constraint *c = array[i];
		int ret;

		if (c->type == OF_IR_CONSTR_SAME_REG) {
			ret = color_reg_constraint(ra, c);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int
chunk_compare(const void *a, const void *b)
{
	const struct of_ir_chunk *ca = *(const struct of_ir_chunk **)a;
	const struct of_ir_chunk *cb = *(const struct of_ir_chunk **)b;

	return cb->cost - ca->cost;
}

static void
prepare_chunk_queue(struct of_ir_reg_assign *ra)
{
	unsigned num_chunks;
	struct of_ir_chunk *c;

	num_chunks = 0;
	LIST_FOR_EACH_ENTRY(c, &ra->chunks, list) {
		if (c->fixed)
			continue;

		util_dynarray_append(&ra->chunk_queue, struct of_ir_chunk *, c);
		++num_chunks;
	}

	qsort(util_dynarray_begin(&ra->chunk_queue), num_chunks,
		sizeof(struct of_ir_chunk *), chunk_compare);
	ra->chunk_queue_len = num_chunks;
}

static void
dump_chunk_queue(struct of_ir_reg_assign *ra)
{
	struct of_ir_chunk **array = util_dynarray_begin(&ra->chunk_queue);
	unsigned i;

	_debug_printf("Chunk queue:\n");

	for (i = 0; i < ra->chunk_queue_len; ++i) {
		struct of_ir_chunk *c = array[i];
		unsigned long *num;

		_debug_printf("{cost = %u, comp = %u}: ", c->cost, c->comp);

		OF_VALSET_FOR_EACH_VAL(num, &c->vars)
			_debug_printf("%lu ", *num);

		_debug_printf("\n");
	}
}

static void
color_chunks(struct of_ir_reg_assign *ra)
{
	struct of_ir_chunk **array = util_dynarray_begin(&ra->chunk_queue);
	unsigned i;

	for (i = 0; i < ra->chunk_queue_len; ++i) {
		struct of_ir_chunk *c = array[i];
		unsigned color = 0;
		unsigned comp_mask;
		unsigned reg;

		if (c->fixed || c->num_vars == 1)
			continue;

		init_reg_bitmap_for_chunk(ra, &ra->reg_bitmap[0], c);

		for (reg = 0; reg < OF_NUM_REGS; ++reg) {
			comp_mask = c->comp ? c->comp : 0xf;

			while (comp_mask) {
				int comp = u_bit_scan(&comp_mask);
				unsigned bit = make_color(reg, comp);

				if (of_bitmap_get(ra->reg_bitmap[0], bit)) {
					color = bit;
					goto done;
				}
			}
		}

		DBG("out of registers?");
		assert(0);

done:
		color_chunk(ra, c, color);
	}
}

static void
precolor(struct of_ir_reg_assign *ra)
{
	prepare_chunks(ra);
	dump_chunks(ra);
	prepare_constraints(ra);
	dump_constraints(ra);
	color_constraints(ra);
	prepare_chunk_queue(ra);
	dump_chunk_queue(ra);
	color_chunks(ra);
}

static struct of_ir_constraint *
create_constraint(struct of_ir_reg_assign *ra, enum of_ir_constraint_type type)
{
	struct of_ir_constraint *c = of_heap_alloc(ra->heap, sizeof(*c));

	c->type = type;
	of_valset_init(&c->vars, &ra->valset_slab);

	util_dynarray_append(&ra->constraints, struct of_ir_constraint *, c);
	++ra->num_constraints;

	return c;
}

static INLINE void
constraint_add_var(struct of_ir_reg_assign *ra, struct of_ir_constraint *c,
		   uint16_t var)
{
	struct of_ir_variable *v = get_var(ra, var);

	of_valset_add(&c->vars, var);
	v->constraints |= c->type;
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

	ins->flags |= OF_IR_INSTR_COPY;

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
	unsigned comp;

	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
		struct of_ir_instruction *copy;
		uint16_t tmp;

		if (!comp_used(reg, comp))
			continue;

		tmp = add_var_num(ra);
		copy = create_copy(ra->shader, tmp, reg->var[comp]);

		if (dst)
			of_ir_instr_insert(ra->shader, NULL, ins, copy);
		else
			of_ir_instr_insert_before(ra->shader, NULL, ins, copy);

		add_affinity(ra, tmp, reg->var[comp], 20000);
	}
}

static void
split_live_list(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins, *s;

	LIST_FOR_EACH_ENTRY_SAFE(ins, s, &node->list.instrs, list) {
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

		tmp = add_var_num(ra);
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

		tmp = add_var_num(ra);
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

	LIST_FOR_EACH_ENTRY_SAFE(child, s, &node->nodes, parent_list)
		split_live(ra, child);

	if (node->type == OF_IR_NODE_REGION) {
		constraint_phi(ra, &node->ssa.phis, node->ssa.depart_count);
		constraint_phi(ra, &node->ssa.loop_phis,
				node->ssa.repeat_count + 1);
	}
}

/*
 * A pass propagating copies introduced in split_live to instruction operands.
 * Essentialy a second pass of live range splitting.
 */

static void
rename_copies_list(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;

	LIST_FOR_EACH_ENTRY(ins, &node->list.instrs, list) {
		struct of_ir_register *dst, *src;
		uint16_t new_src[3][OF_IR_VEC_SIZE];
		unsigned comp;
		unsigned i;

		for (i = 0; i < ins->num_srcs; ++i) {
			src = ins->srcs[i];
			if (src->type != OF_IR_REG_VAR)
				continue;

			for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
				uint16_t var = src->var[comp];

				if (!(src->mask & (1 << comp)))
					continue;

				if (ra->renames[var])
					new_src[i][comp] = ra->renames[var];
				else
					new_src[i][comp] = 0;
			}
		}

		if (ins->flags & OF_IR_INSTR_COPY) {
			dst = ins->dst;
			assert(dst && dst->type == OF_IR_REG_VAR);
			assert(ins->num_srcs == 1);
			src = ins->srcs[0];
			assert(src && src->type == OF_IR_REG_VAR);

			for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
				uint16_t var = src->var[comp];

				if (!(dst->mask & (1 << comp)))
					continue;

				ra->renames[var] = dst->var[comp];
			}
		}

		for (i = 0; i < ins->num_srcs; ++i) {
			src = ins->srcs[i];
			if (src->type != OF_IR_REG_VAR)
				continue;

			for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
				if (!(src->mask & (1 << comp)))
					continue;

				if (new_src[i][comp])
					src->var[comp] = new_src[i][comp];
			}
		}
	}
}

static INLINE void
rename_phi_operand(struct of_ir_reg_assign *ra, unsigned num,
		   struct of_ir_phi *phi, uint16_t *renames)
{
	if (renames[phi->reg])
		phi->src[num] = renames[phi->reg];
}

static void
rename_copies(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *region;
	struct of_ir_ast_node *child;
	struct of_ir_phi *phi;

	switch (node->type) {
	case OF_IR_NODE_REGION:
		LIST_FOR_EACH_ENTRY(phi, &node->ssa.loop_phis, list)
			rename_phi_operand(ra, 0, phi, ra->renames);
		break;

	case OF_IR_NODE_IF_THEN:
		LIST_FOR_EACH_ENTRY(phi, &node->ssa.phis, list)
			rename_phi_operand(ra, 0, phi, ra->renames);
		break;

	case OF_IR_NODE_DEPART:
	case OF_IR_NODE_REPEAT:
		ra->renames = of_stack_push_copy(ra->renames_stack);
		break;

	case OF_IR_NODE_LIST:
		rename_copies_list(ra, node);
		return;
	}

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		rename_copies(ra, child);

	switch (node->type) {
	case OF_IR_NODE_REGION:
		break;

	case OF_IR_NODE_IF_THEN:
		LIST_FOR_EACH_ENTRY(phi, &node->ssa.phis, list)
			rename_phi_operand(ra, 1, phi, ra->renames);
		break;

	case OF_IR_NODE_DEPART:
		region = node->depart_repeat.region;
		LIST_FOR_EACH_ENTRY(phi, &region->ssa.phis, list)
			rename_phi_operand(ra, node->ssa.depart_number, phi,
						ra->renames);

		ra->renames = of_stack_pop(ra->renames_stack);
		break;

	case OF_IR_NODE_REPEAT:
		region = node->depart_repeat.region;
		LIST_FOR_EACH_ENTRY(phi, &region->ssa.loop_phis, list)
			rename_phi_operand(ra, node->ssa.repeat_number, phi,
						ra->renames);

		ra->renames = of_stack_pop(ra->renames_stack);
		break;

	default:
		break;
	}
}

/*
 * Add vector constraints.
 */

static void
constraint_vector(struct of_ir_reg_assign *ra, struct of_ir_register *reg)
{
	struct of_ir_constraint *c;
	unsigned comp;

	c = create_constraint(ra, OF_IR_CONSTR_SAME_REG);

	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
		if (!comp_used(reg, comp))
			continue;

		constraint_add_var(ra, c, reg->var[comp]);
	}
}

static void
constraint_vectors_list(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;

	LIST_FOR_EACH_ENTRY(ins, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;
		const struct of_ir_opc_info *info;
		unsigned i;

		if (dst && dst->type == OF_IR_REG_VAR) {
			info = of_ir_get_opc_info(ins->opc);

			if (info->fix_comp) {
				unsigned comp;

				for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
					uint16_t var = dst->var[comp];

					if (!comp_used(dst, comp))
						continue;

					get_var(ra, var)->comp = (1 << comp);
				}
			}

			if (is_vector(dst))
				constraint_vector(ra, dst);
		}

		for (i = 0; i < OF_IR_NUM_SRCS && ins->srcs[i]; ++i) {
			struct of_ir_register *src = ins->srcs[i];

			if (src->type == OF_IR_REG_VAR && is_vector(src))
				constraint_vector(ra, src);
		}
	}
}

static void
constraint_vectors(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list) {
		if (child->type == OF_IR_NODE_LIST)
			constraint_vectors_list(ra, child);
		else
			constraint_vectors(ra, child);
	}
}

/*
 * Register assignment.
 */

static void
color_var(struct of_ir_reg_assign *ra, struct of_ir_variable *v)
{
	unsigned comp_mask;
	unsigned color;

	init_reg_bitmap(ra, &ra->reg_bitmap[0], v->interference);

	comp_mask = v->comp ? v->comp : 0xf;

	OF_BITMAP_FOR_EACH_SET_BIT(color, ra->reg_bitmap[0],
				   OF_REG_BITMAP_BITS) {
		unsigned comp = color_comp(color);

		if (comp_mask & (1 << comp))
			break;
	}

	assert(color != -1U && "color failed");
	v->color = color;
}

static void
color_register(struct of_ir_reg_assign *ra, struct of_ir_register *reg)
{
	unsigned comp;

	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
		struct of_ir_variable *v = get_var(ra, reg->var[comp]);

		if (!(reg->mask & (1 << comp)))
			continue;

		if (!v->color)
			color_var(ra, v);

		reg->var[comp] = v->color;
	}

	reg->type = OF_IR_REG_VARC;
}

static void
assign_registers_list(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;

	LIST_FOR_EACH_ENTRY(ins, &node->list.instrs, list) {
		unsigned i;

		if (ins->dst && ins->dst->type == OF_IR_REG_VAR)
			color_register(ra, ins->dst);

		for (i = 0; i < ins->num_srcs; ++i)
			if (ins->srcs[i]->type == OF_IR_REG_VAR)
				color_register(ra, ins->srcs[i]);
	}
}

static void
assign_registers(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	if (node->type == OF_IR_NODE_LIST)
		return assign_registers_list(ra, node);

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		assign_registers(ra, child);
}

/*
 * Copy elimination.
 */

static void
copy_elimination_list(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins, *s;

	LIST_FOR_EACH_ENTRY_SAFE(ins, s, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst, *src = ins->srcs[0];
		unsigned comp;

		if (ins->opc != OF_OP_MOV || src->type != OF_IR_REG_VARC
		    || dst->type != OF_IR_REG_VARC)
			continue;

		for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
			if (!(dst->mask & (1 << comp)))
				continue;
			if (src->var[comp] != dst->var[comp])
				break;
		}

		if (comp == OF_IR_VEC_SIZE)
			list_del(&ins->list);
	}
}

static void
copy_elimination(struct of_ir_reg_assign *ra, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child, *s;

	LIST_FOR_EACH_ENTRY_SAFE(child, s, &node->nodes, parent_list) {
		if (child->type == OF_IR_NODE_LIST)
			copy_elimination_list(ra, child);
		else
			copy_elimination(ra, child);
	}
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
		struct of_ir_variable *v = get_var(ra, var1);

		if (!v->interference)
			continue;

		_debug_printf("@%d: ", var1);
		OF_BITMAP_FOR_EACH_SET_BIT(var2, v->interference, ra->num_vars)
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
	LIST_INITHEAD(&ra->chunks);
	util_dynarray_init(&ra->constraints);
	util_dynarray_init(&ra->affinities);
	util_dynarray_init(&ra->vars);
	util_dynarray_resize(&ra->vars, ra->num_vars
				* sizeof(struct of_ir_variable));
	memset(util_dynarray_begin(&ra->vars), 0, ra->num_vars
				* sizeof(struct of_ir_variable));
	util_slab_create(&ra->valset_slab, sizeof(struct of_valset_value),
				64, UTIL_SLAB_SINGLETHREADED);

	RUN_PASS(shader, ra, split_live);
	DBG("AST (post-split-live)");
	of_ir_dump_ast(shader, dump_ra_data, ra);

	ra->renames_stack = of_stack_create(ra->num_vars
						* sizeof(*ra->renames), 1);
	ra->renames = of_stack_top(ra->renames_stack);
	memset(ra->renames, 0, ra->num_vars * sizeof(*ra->renames));
	RUN_PASS(shader, ra, rename_copies);
	of_stack_destroy(ra->renames_stack);
	DBG("AST (post-rename-copies)");
	of_ir_dump_ast(shader, NULL, 0);

	RUN_PASS(shader, ra, constraint_vectors);
	ra->live = CALLOC(1, OF_BITMAP_BYTES_FOR_BITS(ra->num_vars));
	RUN_PASS(shader, ra, interference);
	dump_interference(ra);
	FREE(ra->live);

	util_dynarray_init(&ra->chunk_queue);
	util_slab_create(&ra->chunk_slab, sizeof(struct of_ir_chunk),
				32, UTIL_SLAB_SINGLETHREADED);
	precolor(ra);
	RUN_PASS(shader, ra, assign_registers);
	DBG("AST (post-register-assignment)");
	of_ir_dump_ast(shader, NULL, 0);

	RUN_PASS(shader, ra, copy_elimination);
	DBG("AST (post-copy-elimination)");
	of_ir_dump_ast(shader, NULL, 0);

	util_dynarray_fini(&ra->vars);
	util_dynarray_fini(&ra->affinities);
	util_dynarray_fini(&ra->constraints);
	util_slab_destroy(&ra->chunk_slab);
	util_slab_destroy(&ra->valset_slab);
	of_heap_destroy(heap);

	return 0;
}
