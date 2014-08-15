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

#include "openfimg_ir_priv.h"
#include "openfimg_util.h"

#define VERBOSE_DUMP	0

#if VERBOSE_DUMP
#define VERBOSE(code)	do {			\
		code;					\
	} while(0)
#else
#define VERBOSE(code)
#endif

enum of_ir_constraint_type {
	OF_IR_CONSTR_SAME_REG = BIT(0),
	OF_IR_CONSTR_PHI = BIT(1),
};

struct of_ir_chunk {
	struct list_head list;
	struct of_valset vars;
	unsigned num_vars;
	unsigned cost;
	unsigned color;
	uint8_t parity;
	uint8_t comp;
	unsigned fixed :1;
	unsigned prealloc :1;
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

/*
 * Interference graph helpers.
 */

static INLINE bool
vars_interference(struct of_ir_optimizer *opt, uint16_t var1, uint16_t var2)
{
	struct of_ir_variable *v1 = get_var(opt, var1);

	assert(var1 < opt->num_vars);
	assert(var2 < opt->num_vars);

	return v1->interference && of_bitmap_get(v1->interference, var2);
}

static void
remove_interference(struct of_ir_optimizer *opt, uint16_t var1, uint16_t var2)
{
	struct of_ir_variable *v1 = get_var(opt, var1);
	struct of_ir_variable *v2 = get_var(opt, var2);

	if (!v1->interference)
		return;

	assert(v2->interference);

	of_bitmap_clear(v1->interference, var2);
	of_bitmap_clear(v2->interference, var1);
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
create_chunk(struct of_ir_optimizer *opt, struct of_ir_variable *var)
{
	struct of_ir_chunk *c = util_slab_alloc(&opt->chunk_slab);
	uint16_t num = var_num(opt, var);

	memset(c, 0, sizeof(*c));

	list_addtail(&c->list, &opt->chunks);

	if (var->chunk) {
		of_valset_del(&var->chunk->vars, num);
		--var->chunk->num_vars;
	}

	of_valset_init(&c->vars, &opt->valset_slab);
	of_valset_add(&c->vars, num);

	c->num_vars = 1;
	c->comp = var->comp;
	c->parity = var->parity;
	var->chunk = c;

	return c;
}

static void
destroy_chunk(struct of_ir_optimizer *opt, struct of_ir_chunk *c)
{
	list_del(&c->list);
	util_slab_free(&opt->chunk_slab, c);
}

static void
try_to_merge_chunks(struct of_ir_optimizer *opt, struct of_ir_affinity *a,
		    struct of_ir_chunk *c0, struct of_ir_chunk *c1)
{
	unsigned long *num0, *num1;

	if (c0->comp && c1->comp && c0->comp != c1->comp)
		return;

	if ((c0->parity | c1->parity) == 0x3)
		return;

	OF_VALSET_FOR_EACH_VAL(num0, &c0->vars) {
		OF_VALSET_FOR_EACH_VAL(num1, &c1->vars) {
			if (*num0 == *num1)
				continue;
			if (vars_interference(opt, *num0, *num1))
				return;
		}
	}

	c0->num_vars += c1->num_vars;
	c0->comp |= c1->comp;
	c0->parity |= c1->parity;
	c0->cost += a->cost;

	OF_VALSET_FOR_EACH_VAL(num1, &c1->vars) {
		struct of_ir_variable *v1 = get_var(opt, *num1);

		of_valset_add(&c0->vars, *num1);
		v1->chunk = c0;
	}

	destroy_chunk(opt, c1);
}

static void
prepare_chunks(struct of_ir_optimizer *opt)
{
	struct of_ir_affinity **array = util_dynarray_begin(&opt->affinities);
	unsigned i;

	qsort(array, opt->num_affinities, sizeof(struct of_ir_affinity *),
		affinity_compare);

	for (i = 0; i < opt->num_affinities; ++i) {
		struct of_ir_affinity *a = array[i];
		struct of_ir_variable *v0 = get_var(opt, a->vars[0]);
		struct of_ir_variable *v1 = get_var(opt, a->vars[1]);

		VERBOSE(DBG("v0 = %u, v1 = %u", a->vars[0], a->vars[1]));

		if (!v0->chunk)
			create_chunk(opt, v0);

		if (!v1->chunk)
			create_chunk(opt, v1);

		if (v0->chunk == v1->chunk) {
			v1->chunk->cost += a->cost;
			continue;
		}

		try_to_merge_chunks(opt, a, v0->chunk, v1->chunk);
	}
}

#if VERBOSE_DUMP
static void
dump_chunks(struct of_ir_optimizer *opt)
{
	struct of_ir_chunk *c;

	_debug_printf("Coalescer chunks:\n");

	LIST_FOR_EACH_ENTRY(c, &opt->chunks, list) {
		unsigned long *num;

		_debug_printf("{cost = %u, comp = %x, parity = %x}: ",
				c->cost, c->comp, c->parity);

		OF_VALSET_FOR_EACH_VAL(num, &c->vars)
			_debug_printf("%lu ", *num);

		_debug_printf("\n");
	}
}
#endif

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
prepare_constraints(struct of_ir_optimizer *opt)
{
	struct of_ir_constraint **array = util_dynarray_begin(&opt->constraints);
	unsigned i;

	for (i = 0; i < opt->num_constraints; ++i) {
		struct of_ir_constraint *c = array[i];
		unsigned long *num;

		if (c->type != OF_IR_CONSTR_SAME_REG || c->num_vars <= 1)
			continue;

		OF_VALSET_FOR_EACH_VAL(num, &c->vars) {
			struct of_ir_variable *v = get_var(opt, *num);

			if (!v->chunk)
				create_chunk(opt, v);
			else
				c->cost += v->chunk->cost;
		}
	}

	qsort(array, opt->num_constraints, sizeof(*array), constraint_compare);
}

#if VERBOSE_DUMP
static void
dump_constraints(struct of_ir_optimizer *opt)
{
	struct of_ir_constraint **array = util_dynarray_begin(&opt->constraints);
	unsigned i;

	_debug_printf("Coloring constraints:\n");

	for (i = 0; i < opt->num_constraints; ++i) {
		struct of_ir_constraint *c = array[i];
		unsigned long *num;

		_debug_printf("{type = %u, cost = %u}: ", c->type, c->cost);

		OF_VALSET_FOR_EACH_VAL(num, &c->vars)
			_debug_printf("%u ", *num);

		_debug_printf("\n");
	}
}
#endif

static bool
next_swizzle(uint8_t swz[4])
{
	int k = -1, l = -1;
	unsigned i;

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
color_chunk(struct of_ir_optimizer *opt, struct of_ir_chunk *c, unsigned color)
{
	uint8_t comp = color_comp(color);
	uint16_t reg = color_reg(color);
	unsigned long *num;

	OF_VALSET_FOR_EACH_VAL(num, &c->vars) {
		struct of_ir_variable *v = get_var(opt, *num);

		if ((v->parity & BIT(reg % 2))
		    || (v->comp && !(v->comp & BIT(comp)))) {
			create_chunk(opt, v);
			continue;
		}

		v->color = color;

		if (v->constraints & OF_IR_CONSTR_PHI) {
			v->fixed = 1;
			c->fixed = 1;
		}

		VERBOSE(DBG("assigned R%u.%c to @%u",
			color_reg(color), "xyzw"[comp], *num));
	}

	c->color = color;
}

static void
init_reg_bitmap(struct of_ir_optimizer *opt, uint32_t **regs,
		uint32_t *interf)
{
	struct of_ir_variable *v;
	uint32_t *bitmap;
	unsigned var;

	bitmap = *regs;
	if (!bitmap)
		*regs = bitmap = of_heap_alloc(opt->heap, OF_REG_BITMAP_SIZE);

	memset(bitmap, 0xff, OF_REG_BITMAP_SIZE);
	of_bitmap_clear(bitmap, 0);

	if (!interf)
		return;

	/* Mark registers already assigned to interfering variables. */
	OF_BITMAP_FOR_EACH_SET_BIT(var, interf, opt->num_vars) {
		v = get_var(opt, var);
		if (v->color)
			of_bitmap_clear(bitmap, v->color);
	}
}

static void
init_reg_bitmap_for_chunk(struct of_ir_optimizer *opt, uint32_t **regs,
			  struct of_ir_chunk *c)
{
	struct of_ir_variable *v;
	unsigned long *num;
	uint32_t *interf;

	interf = opt->chunk_interf;
	if (!interf)
		opt->chunk_interf = interf = of_heap_alloc(opt->heap,
					OF_BITMAP_BYTES_FOR_BITS(opt->num_vars));
	memset(interf, 0, OF_BITMAP_BYTES_FOR_BITS(opt->num_vars));

	/* Calculate interference of the chunk. */
	OF_VALSET_FOR_EACH_VAL(num, &c->vars) {
		v = get_var(opt, *num);
		if (!v->interference)
			continue;
		of_bitmap_or(interf, interf, v->interference, opt->num_vars);
	}

	OF_VALSET_FOR_EACH_VAL(num, &c->vars)
		of_bitmap_clear(interf, *num);

	init_reg_bitmap(opt, regs, interf);
}

static int
color_reg_constraint(struct of_ir_optimizer *opt, struct of_ir_constraint *c)
{
	uint8_t swz[OF_IR_VEC_SIZE] = {0, 1, 2, 3};
	struct of_ir_chunk *ch[OF_IR_VEC_SIZE];
	unsigned parity_mask = 0;
	unsigned comp_mask = 0;
	unsigned long *num;
	unsigned reg;
	unsigned i;

	if (c->num_vars <= 1)
		return 0;

	i = 0;
	OF_VALSET_FOR_EACH_VAL(num, &c->vars) {
		struct of_ir_variable *v = get_var(opt, *num);

		if (!v->chunk)
			create_chunk(opt, v);

		ch[i] = v->chunk;

		if ((parity_mask | ch[i]->parity) == 0x3) {
			ch[i] = create_chunk(opt, v);
			assert(!ch[i]->parity);
		} else if (v->chunk->comp & comp_mask) {
			ch[i] = create_chunk(opt, v);
			assert(!ch[i]->comp);
		}

		comp_mask |= v->chunk->comp;
		parity_mask |= ch[i]->parity;

		init_reg_bitmap_for_chunk(opt, &opt->reg_bitmap[i], ch[i]);
		++i;
	}

	assert(parity_mask != 0x3);

	do {
		for (i = 0; i < c->num_vars; ++i)
			if (ch[i]->comp && ch[i]->comp != BIT(swz[i]))
				break;
		if (i != c->num_vars)
			continue;

		for (reg = 0; reg < OF_NUM_REGS - 1; ++reg) {
			if (parity_mask & BIT(reg % 2))
				continue;
			for (i = 0; i < c->num_vars; ++i) {
				unsigned color = make_color(reg, swz[i]);

				if (!of_bitmap_get(opt->reg_bitmap[i], color))
					break;
			}
			if (i == c->num_vars)
				goto done;
		}
	} while (next_swizzle(swz));

	DBG("out of registers");
	return -1;

done:
	VERBOSE(DBG("reg = %u, swz = (%d,%d,%d,%d)",
		reg, swz[0], swz[1], swz[2], swz[3]));

	i = 0;
	OF_VALSET_FOR_EACH_VAL(num, &c->vars) {
		struct of_ir_variable *v = get_var(opt, *num);
		unsigned color = make_color(reg, swz[i]);
		struct of_ir_chunk *cc = ch[i];

		if (cc->fixed) {
			if (cc->color == color) {
				++i;
				continue;
			}
			cc = create_chunk(opt, v);
		}

		color_chunk(opt, cc, color);
		cc->fixed = 1;
		cc->prealloc = 1;
		cc->comp = BIT(swz[i]);
		++i;
	}

	return 0;
}

static int
color_constraints(struct of_ir_optimizer *opt)
{
	struct of_ir_constraint **array = util_dynarray_begin(&opt->constraints);
	unsigned i;

	for (i = 0; i < opt->num_constraints; ++i) {
		struct of_ir_constraint *c = array[i];
		int ret;

		if (c->type == OF_IR_CONSTR_SAME_REG) {
			ret = color_reg_constraint(opt, c);
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
prepare_chunk_queue(struct of_ir_optimizer *opt)
{
	struct of_ir_chunk *c;
	unsigned num_chunks;

	num_chunks = 0;
	LIST_FOR_EACH_ENTRY(c, &opt->chunks, list) {
		if (c->fixed)
			continue;

		util_dynarray_append(&opt->chunk_queue, struct of_ir_chunk *, c);
		++num_chunks;
	}

	qsort(util_dynarray_begin(&opt->chunk_queue), num_chunks,
		sizeof(struct of_ir_chunk *), chunk_compare);
	opt->chunk_queue_len = num_chunks;
}

#if VERBOSE_DUMP
static void
dump_chunk_queue(struct of_ir_optimizer *opt)
{
	struct of_ir_chunk **array = util_dynarray_begin(&opt->chunk_queue);
	unsigned i;

	_debug_printf("Chunk queue:\n");

	for (i = 0; i < opt->chunk_queue_len; ++i) {
		struct of_ir_chunk *c = array[i];
		unsigned long *num;

		_debug_printf("{cost = %u, comp = %u}: ", c->cost, c->comp);

		OF_VALSET_FOR_EACH_VAL(num, &c->vars)
			_debug_printf("%lu ", *num);

		_debug_printf("\n");
	}
}
#endif

static void
color_chunks(struct of_ir_optimizer *opt)
{
	struct of_ir_chunk **array = util_dynarray_begin(&opt->chunk_queue);
	unsigned i;

	for (i = 0; i < opt->chunk_queue_len; ++i) {
		struct of_ir_chunk *c = array[i];
		unsigned color = 0;
		unsigned comp_mask;
		unsigned reg;

		if (c->fixed || c->num_vars == 1)
			continue;

		init_reg_bitmap_for_chunk(opt, &opt->reg_bitmap[0], c);

		for (reg = 0; reg < OF_NUM_REGS; ++reg) {
			if (c->parity & BIT(reg % 2))
				continue;

			comp_mask = c->comp ? c->comp : 0xf;

			while (comp_mask) {
				int comp = u_bit_scan(&comp_mask);
				unsigned bit = make_color(reg, comp);

				if (of_bitmap_get(opt->reg_bitmap[0], bit)) {
					color = bit;
					goto done;
				}
			}
		}

		DBG("out of registers?");
		assert(0);

done:
		color_chunk(opt, c, color);
	}
}

static void
precolor(struct of_ir_optimizer *opt)
{
	prepare_chunks(opt);
	VERBOSE(dump_chunks(opt));
	prepare_constraints(opt);
	VERBOSE(dump_constraints(opt));
	color_constraints(opt);
	prepare_chunk_queue(opt);
	VERBOSE(dump_chunk_queue(opt));
	color_chunks(opt);
}

static struct of_ir_constraint *
create_constraint(struct of_ir_optimizer *opt, enum of_ir_constraint_type type)
{
	struct of_ir_constraint *c = of_heap_alloc(opt->heap, sizeof(*c));

	c->type = type;
	of_valset_init(&c->vars, &opt->valset_slab);

	util_dynarray_append(&opt->constraints, struct of_ir_constraint *, c);
	++opt->num_constraints;

	return c;
}

static INLINE void
constraint_add_var(struct of_ir_optimizer *opt, struct of_ir_constraint *c,
		   uint16_t var)
{
	struct of_ir_variable *v = get_var(opt, var);

	of_valset_add(&c->vars, var);
	v->constraints |= c->type;
	++c->num_vars;
}

static void
add_affinity(struct of_ir_optimizer *opt, uint16_t var1, uint16_t var2,
	     unsigned cost)
{
	struct of_ir_affinity *a = of_heap_alloc(opt->heap, sizeof(*a));

	a->vars[0] = var1;
	a->vars[1] = var2;
	a->cost = cost;

	util_dynarray_append(&opt->affinities, struct of_ir_affinity *, a);
	++opt->num_affinities;
}

/*
 * Live range splitting.
 */

static struct of_ir_instruction *
create_copy(struct of_ir_optimizer *opt, uint16_t dst_var, uint16_t src_var)
{
	struct of_ir_register *dst, *src;
	struct of_ir_instruction *ins;

	ins = of_ir_instr_create(opt->shader, OF_OP_MOV);

	dst = of_ir_reg_create(opt->shader, OF_IR_REG_VAR, 0, "x___", 0);
	of_ir_instr_add_dst(ins, dst);
	dst->var[0] = dst_var;
	dst->mask = 1;

	src = of_ir_reg_create(opt->shader, OF_IR_REG_VAR, 0, "xxxx", 0);
	of_ir_instr_add_src(ins, src);
	src->var[0] = src_var;
	src->mask = 1;

	ins->flags |= OF_IR_INSTR_COPY;

	add_affinity(opt, dst_var, src_var, 1);

	return ins;
}

static void
split_operand(struct of_ir_optimizer *opt, struct of_ir_instruction *ins,
	      struct of_ir_register *reg, bool dst)
{
	uint16_t vars[OF_IR_VEC_SIZE];
	uint16_t copies[OF_IR_VEC_SIZE];
	unsigned comp;
	unsigned cnt;

	cnt = 0;
	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
		struct of_ir_instruction *copy;
		uint16_t tmp2;
		uint16_t var;
		uint16_t tmp;
		unsigned i;

		if (!reg_comp_used(reg, comp))
			continue;

		var = reg->var[comp];

		if (!var)
			continue;

		for (i = 0; i < cnt; ++i)
			if (vars[i] == var)
				break;
		if (i != cnt) {
			reg->var[comp] = copies[i];
			continue;
		}

		tmp = add_var_num(opt);
		add_affinity(opt, tmp, var, 20000);

		if (dst) {
			copy = create_copy(opt, var, tmp);
			of_ir_instr_insert(opt->shader, NULL, ins, copy);
		} else {
			copy = create_copy(opt, tmp, var);
			of_ir_instr_insert_before(opt->shader, NULL, ins, copy);

			tmp2 = add_var_num(opt);
			copy = create_copy(opt, tmp2, tmp);
			of_ir_instr_insert(opt->shader, NULL, ins, copy);
			add_affinity(opt, var, tmp2, 20000);
		}

		reg->var[comp] = tmp;
		copies[cnt] = tmp;
		vars[cnt++] = var;
	}
}

static void
split_live_list(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins, *s;

	LIST_FOR_EACH_ENTRY_SAFE(ins, s, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;
		unsigned tmp_srcs = 0;
		unsigned split = 0x7;
		unsigned i;

		if (dst && dst->type == OF_IR_REG_VAR && reg_is_vector(dst))
			split_operand(opt, ins, dst, true);

		for (i = 0; i < ins->num_srcs; ++i) {
			struct of_ir_register *src = ins->srcs[i];

			if (src->type == OF_IR_REG_VAR) {
				if (reg_is_vector(src)) {
					split_operand(opt, ins, src, false);
					split &= ~BIT(i);
				}
				++tmp_srcs;
			}
		}

		if (tmp_srcs != 3)
			continue;

		/* NOTE: 3-source operations with 3 temporary operands must be
		 * also constrained and so range must be split for operands
		 * which were not handled by the loop above. */
		while (split) {
			i = u_bit_scan(&split);
			split_operand(opt, ins, ins->srcs[i], false);
		}
	}
}

static void
split_live_phi_src(struct of_ir_optimizer *opt, struct of_ir_ast_node *node,
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

		tmp = add_var_num(opt);
		ins = create_copy(opt, tmp, phi->src[arg]);
		of_ir_instr_insert(node->shader, list, NULL, ins);
	}
}

static void
split_live_phi_dst(struct of_ir_optimizer *opt, struct of_ir_ast_node *node,
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

		tmp = add_var_num(opt);
		ins = create_copy(opt, phi->dst, tmp);
		of_ir_instr_insert_before(node->shader, list, NULL, ins);
		phi->dst = tmp;
	}
}

static void
split_live(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child, *s;
	struct of_ir_ast_node *region;

	switch (node->type) {
	case OF_IR_NODE_LIST:
		return split_live_list(opt, node);

	case OF_IR_NODE_DEPART:
		region = node->depart_repeat.region;
		split_live_phi_src(opt, node, &region->ssa.phis,
					node->ssa.depart_number, false);
		break;

	case OF_IR_NODE_REPEAT:
		region = node->depart_repeat.region;
		split_live_phi_src(opt, node, &region->ssa.loop_phis,
					node->ssa.repeat_number, true);
		break;

	case OF_IR_NODE_REGION:
		split_live_phi_dst(opt, node, &node->ssa.phis, false);
		split_live_phi_dst(opt, node, &node->ssa.loop_phis, true);
		split_live_phi_src(opt, node, &node->ssa.loop_phis, 0, true);
		break;

	default:
		break;
	}

	LIST_FOR_EACH_ENTRY_SAFE(child, s, &node->nodes, parent_list)
		split_live(opt, child);
}

/*
 * A pass propagating copies introduced in split_live to instruction operands.
 * Essentialy a second pass of live range splitting.
 */

static void
rename_copies_list(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
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

				if (!reg_comp_used(src, comp))
					continue;

				if (opt->renames[var])
					new_src[i][comp] = opt->renames[var];
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

				if (!reg_comp_used(dst, comp))
					continue;

				opt->renames[var] = dst->var[comp];
			}
		}

		for (i = 0; i < ins->num_srcs; ++i) {
			src = ins->srcs[i];
			if (src->type != OF_IR_REG_VAR)
				continue;

			for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
				if (!reg_comp_used(src, comp))
					continue;

				if (new_src[i][comp])
					src->var[comp] = new_src[i][comp];
			}
		}
	}
}

static INLINE void
rename_phi_operand(struct of_ir_optimizer *opt, unsigned num,
		   struct of_ir_phi *phi, uint16_t *renames)
{
	uint16_t var = phi->src[num];
	if (renames[var])
		phi->src[num] = renames[var];
}

static void
rename_copies(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *region;
	struct of_ir_ast_node *child;
	struct of_ir_phi *phi;

	switch (node->type) {
	case OF_IR_NODE_REGION:
		LIST_FOR_EACH_ENTRY(phi, &node->ssa.loop_phis, list)
			rename_phi_operand(opt, 0, phi, opt->renames);
		break;

	case OF_IR_NODE_IF_THEN:
		LIST_FOR_EACH_ENTRY(phi, &node->ssa.phis, list)
			rename_phi_operand(opt, 0, phi, opt->renames);
		break;

	case OF_IR_NODE_DEPART:
	case OF_IR_NODE_REPEAT:
		opt->renames = of_stack_push_copy(opt->renames_stack);
		break;

	case OF_IR_NODE_LIST:
		rename_copies_list(opt, node);
		return;
	}

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		rename_copies(opt, child);

	switch (node->type) {
	case OF_IR_NODE_REGION:
		break;

	case OF_IR_NODE_IF_THEN:
		LIST_FOR_EACH_ENTRY(phi, &node->ssa.phis, list)
			rename_phi_operand(opt, 1, phi, opt->renames);
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
 * Add vector constraints.
 */

static void
constraint_vector(struct of_ir_optimizer *opt, struct of_ir_register *reg)
{
	uint16_t vars[OF_IR_VEC_SIZE];
	struct of_ir_constraint *c;
	unsigned comp;
	unsigned cnt;

	c = create_constraint(opt, OF_IR_CONSTR_SAME_REG);

	cnt = 0;
	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
		uint16_t var;
		unsigned i;

		if (!reg_comp_used(reg, comp))
			continue;

		var = reg->var[comp];
		for (i = 0; i < cnt; ++i)
			if (vars[i] == var)
				break;
		if (i != cnt)
			continue;

		constraint_add_var(opt, c, var);
		vars[cnt++] = var;
	}
}

static void
add_constraints_list(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;

	LIST_FOR_EACH_ENTRY(ins, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;
		const struct of_ir_opc_info *info;
		struct of_ir_register *src;
		unsigned tmp_srcs = 0;
		unsigned comp;
		unsigned i;

		if (ins->flags & OF_IR_INSTR_COPY) {
			assert(dst && dst->type == OF_IR_REG_VAR);
			assert(ins->num_srcs == 1);
			src = ins->srcs[0];
			assert(src && src->type == OF_IR_REG_VAR);

			for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
				if (!reg_comp_used(dst, comp))
					continue;
				remove_interference(opt, src->var[comp],
							dst->var[comp]);
			}
		}

		if (dst && dst->type == OF_IR_REG_VAR) {
			info = of_ir_get_opc_info(ins->opc);

			if (info->fix_comp) {
				for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
					uint16_t var = dst->var[comp];

					if (!reg_comp_used(dst, comp))
						continue;

					get_var(opt, var)->comp = BIT(comp);
				}
			}

			if (reg_is_vector(dst))
				constraint_vector(opt, dst);
		}

		for (i = 0; i < ins->num_srcs; ++i) {
			src = ins->srcs[i];
			if (src->type == OF_IR_REG_VAR) {
				if (reg_is_vector(src))
					constraint_vector(opt, src);
				++tmp_srcs;
			}
		}

		if (tmp_srcs != 3)
			continue;

		for (i = 0; i < ins->num_srcs; ++i) {
			struct of_ir_register *src = ins->srcs[i];

			for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
				uint16_t var = src->var[comp];

				if (!reg_comp_used(src, comp))
					continue;

				get_var(opt, var)->parity = BIT(opt->parity);
			}

			opt->parity ^= 1;
		}
	}
}

static void
add_constraints_phi(struct of_ir_optimizer *opt, struct list_head *phis,
		    unsigned num_srcs)
{
	struct of_ir_phi *phi;

	LIST_FOR_EACH_ENTRY(phi, phis, list) {
		struct of_ir_constraint *c;
		unsigned i;

		c = create_constraint(opt, OF_IR_CONSTR_PHI);
		constraint_add_var(opt, c, phi->dst);

		for (i = 0; i < num_srcs; ++i) {
			if (!phi->src[i])
				continue;
			constraint_add_var(opt, c, phi->src[i]);
			add_affinity(opt, phi->src[i], phi->dst, 30000);
		}
	}
}

static void
add_constraints(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	add_constraints_phi(opt, &node->ssa.loop_phis,
				node->ssa.repeat_count + 1);

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list) {
		if (child->type == OF_IR_NODE_LIST)
			add_constraints_list(opt, child);
		else
			add_constraints(opt, child);
	}

	add_constraints_phi(opt, &node->ssa.phis, node->ssa.depart_count);
}

/*
 * Color assignment.
 */

static void
color_var(struct of_ir_optimizer *opt, struct of_ir_variable *v)
{
	unsigned comp_mask;
	unsigned color;

	init_reg_bitmap(opt, &opt->reg_bitmap[0], v->interference);

	comp_mask = v->comp ? v->comp : 0xf;

	OF_BITMAP_FOR_EACH_SET_BIT(color, opt->reg_bitmap[0],
				   OF_REG_BITMAP_BITS) {
		unsigned comp = color_comp(color);
		unsigned reg = color_reg(color);

		if (v->parity & BIT(reg % 2))
			continue;

		if (comp_mask & BIT(comp))
			break;
	}

	assert(color != -1U && "color failed");
	v->color = color;
}

static void
color_operand(struct of_ir_optimizer *opt, struct of_ir_register *reg)
{
	unsigned comp;

	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
		struct of_ir_variable *v = get_var(opt, reg->var[comp]);

		if (!reg_comp_used(reg, comp))
			continue;

		if (!v->color)
			color_var(opt, v);

		reg->var[comp] = v->color;
	}

	reg->type = OF_IR_REG_VARC;
}

static void
assign_colors_list(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;

	LIST_FOR_EACH_ENTRY(ins, &node->list.instrs, list) {
		unsigned i;

		if (ins->dst && ins->dst->type == OF_IR_REG_VAR)
			color_operand(opt, ins->dst);

		for (i = 0; i < ins->num_srcs; ++i)
			if (ins->srcs[i]->type == OF_IR_REG_VAR)
				color_operand(opt, ins->srcs[i]);
	}
}

static void
assign_colors(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	if (node->type == OF_IR_NODE_LIST)
		return assign_colors_list(opt, node);

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		assign_colors(opt, child);
}

/*
 * Copy elimination.
 */

static void
copy_elimination_list(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins, *s;

	LIST_FOR_EACH_ENTRY_SAFE(ins, s, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst, *src = ins->srcs[0];
		unsigned comp;

		if (ins->opc != OF_OP_MOV || src->type != OF_IR_REG_VARC
		    || dst->type != OF_IR_REG_VARC || src->flags || dst->flags)
			continue;

		for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
			if (!reg_comp_used(dst, comp))
				continue;
			if (src->var[comp] != dst->var[comp])
				break;
		}

		if (comp == OF_IR_VEC_SIZE)
			list_del(&ins->list);
	}
}

static void
copy_elimination(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child, *s;

	LIST_FOR_EACH_ENTRY_SAFE(child, s, &node->nodes, parent_list) {
		if (child->type == OF_IR_NODE_LIST)
			copy_elimination_list(opt, child);
		else
			copy_elimination(opt, child);
	}
}

/*
 * Register assignment.
 */

static void
remap_sources(struct of_ir_instruction *ins, const struct of_ir_opc_info *info,
	      const uint8_t *map)
{
	unsigned i;

	for (i = 0; i < ins->num_srcs; ++i) {
		struct of_ir_register *src = ins->srcs[i];
		char swizzle[4];
		unsigned comp;

		/* NOTE: Texture components can be shuffled using swizzle
		 * bits of sampler source register which is always src 1. */
		if (info->tex && i != 1)
			continue;

		for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp)
			swizzle[comp] = src->swizzle[map[comp]];

		memcpy(src->swizzle, swizzle, sizeof(src->swizzle));
	}
}

static void
assign_destination(struct of_ir_instruction *ins, struct of_ir_register *dst)
{
	uint8_t chan_map[4] = {0, 1, 2, 3};
	const struct of_ir_opc_info *info;
	bool need_remap = false;
	unsigned mask = 0;
	unsigned comp;

	info = of_ir_get_opc_info(ins->opc);

	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
		uint16_t reg = color_reg(dst->var[comp]);
		uint16_t chan = color_comp(dst->var[comp]);

		if (!reg_comp_used(dst, comp))
			continue;

		if (!info->replicated && chan != comp) {
			assert(!info->fix_comp
				&& "swizzling with fixed components");
			need_remap = true;
			chan_map[chan] = comp;
		}

		dst->num = reg;
		assert(!(mask & BIT(chan)) && "duplicate channel in dst?");
		mask |= BIT(chan);
	}

	if (need_remap)
		remap_sources(ins, info, chan_map);

	dst->type = OF_IR_REG_R;
	dst->mask = mask;
}

static void
assign_source(struct of_ir_register *src)
{
	char safe_chan = 0;
	unsigned comp;

	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
		uint16_t reg = color_reg(src->var[comp]);
		uint16_t chan = color_comp(src->var[comp]);

		if (!reg_comp_used(src, comp))
			continue;

		src->num = reg;
		safe_chan = src->swizzle[comp] = chan;
	}

	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
		if (reg_comp_used(src, comp)) {
			uint16_t reg = color_reg(src->var[comp]);

			assert(src->num == reg
				&& "different registers in single operand");
			continue;
		}
		src->swizzle[comp] = safe_chan;
	}

	src->type = OF_IR_REG_R;
}

static void
assign_registers_list(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;

	LIST_FOR_EACH_ENTRY(ins, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;
		unsigned i;

		for (i = 0; i < ins->num_srcs; ++i) {
			struct of_ir_register *src = ins->srcs[i];

			if (src->type == OF_IR_REG_VARC)
				assign_source(src);
			src->mask = 0xf;
		}

		if (dst && dst->type == OF_IR_REG_VARC)
			assign_destination(ins, dst);
	}
}

static void
assign_registers(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	if (node->type == OF_IR_NODE_LIST)
		return assign_registers_list(opt, node);

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		assign_registers(opt, child);
}

/*
 * AST dumping.
 */
#if VERBOSE_DUMP
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

static void
dump_interference(struct of_ir_optimizer *opt)
{
	unsigned var1, var2;

	for (var1 = 0; var1 < opt->num_vars; ++var1) {
		struct of_ir_variable *v = get_var(opt, var1);

		if (!v->interference)
			continue;

		_debug_printf("@%d: ", var1);
		OF_BITMAP_FOR_EACH_SET_BIT(var2, v->interference, opt->num_vars)
			_debug_printf("@%d ", var2);
		_debug_printf("\n");
	}
}
#endif

int
of_ir_assign_registers(struct of_ir_shader *shader)
{
	struct of_ir_optimizer *opt;
	struct of_heap *heap;

	heap = of_heap_create();
	opt = of_heap_alloc(heap, sizeof(*opt));
	opt->shader = shader;
	opt->heap = heap;
	opt->num_vars = shader->stats.num_vars;
	LIST_INITHEAD(&opt->chunks);
	util_dynarray_init(&opt->constraints);
	util_dynarray_init(&opt->affinities);
	util_dynarray_init(&opt->vars);
	util_dynarray_resize(&opt->vars, opt->num_vars
				* sizeof(struct of_ir_variable));
	memset(util_dynarray_begin(&opt->vars), 0, opt->num_vars
				* sizeof(struct of_ir_variable));
	util_slab_create(&opt->valset_slab, sizeof(struct of_valset_value),
				64, UTIL_SLAB_SINGLETHREADED);

	RUN_PASS(shader, opt, split_live);
	VERBOSE(DBG("AST (post-split-live)"));
	VERBOSE(of_ir_dump_ast(shader, dump_opt_data, opt));

	opt->renames_stack = of_stack_create(opt->num_vars
						* sizeof(*opt->renames), 1);
	opt->renames = of_stack_top(opt->renames_stack);
	memset(opt->renames, 0, opt->num_vars * sizeof(*opt->renames));
	RUN_PASS(shader, opt, rename_copies);
	of_stack_destroy(opt->renames_stack);
	VERBOSE(DBG("AST (post-rename-copies)"));
	VERBOSE(of_ir_dump_ast(shader, dump_opt_data, opt));

	util_slab_create(&opt->live_slab, OF_BITMAP_BYTES_FOR_BITS(opt->num_vars),
				32, UTIL_SLAB_SINGLETHREADED);
	opt->live = util_slab_alloc(&opt->live_slab);
	of_bitmap_fill(opt->live, 0, opt->num_vars);
	opt->want_interference = true;
	RUN_PASS(shader, opt, liveness);
	RUN_PASS(shader, opt, cleanup);
	RUN_PASS(shader, opt, add_constraints);
	VERBOSE(dump_interference(opt));
	util_slab_destroy(&opt->live_slab);
	VERBOSE(DBG("AST (post-liveness2)"));
	VERBOSE(of_ir_dump_ast(shader, NULL, 0));

	util_dynarray_init(&opt->chunk_queue);
	util_slab_create(&opt->chunk_slab, sizeof(struct of_ir_chunk),
				32, UTIL_SLAB_SINGLETHREADED);
	precolor(opt);
	RUN_PASS(shader, opt, assign_colors);
	VERBOSE(DBG("AST (post-color-assignment)"));
	VERBOSE(of_ir_dump_ast(shader, NULL, 0));

	RUN_PASS(shader, opt, copy_elimination);
	DBG("AST (post-copy-elimination)");
	of_ir_dump_ast(shader, NULL, 0);

	RUN_PASS(shader, opt, assign_registers);

	util_dynarray_fini(&opt->vars);
	util_dynarray_fini(&opt->affinities);
	util_dynarray_fini(&opt->constraints);
	util_slab_destroy(&opt->chunk_slab);
	util_slab_destroy(&opt->valset_slab);
	of_heap_destroy(heap);

	return 0;
}
