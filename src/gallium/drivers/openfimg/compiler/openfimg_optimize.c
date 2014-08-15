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

/*
 * Liveness analysis with optional computation of interferences.
 */

static void
add_interference(struct of_ir_optimizer *opt, uint16_t var1, uint16_t var2)
{
	struct of_ir_variable *v1 = get_var(opt, var1);
	struct of_ir_variable *v2 = get_var(opt, var2);

	assert(var1 < opt->num_vars);
	assert(var2 < opt->num_vars);

	if (!v1->interference)
		v1->interference = of_heap_alloc(opt->heap,
					OF_BITMAP_BYTES_FOR_BITS(opt->num_vars));

	if (!v2->interference)
		v2->interference = of_heap_alloc(opt->heap,
					OF_BITMAP_BYTES_FOR_BITS(opt->num_vars));

	of_bitmap_set(v1->interference, var2);
	of_bitmap_set(v2->interference, var1);
}

static void
liveness_src(struct of_ir_optimizer *opt, struct of_ir_register *dst,
	     struct of_ir_register *src, const dst_map_t *dst_map)
{
	unsigned scomp;

	for (scomp = 0; scomp < OF_IR_VEC_SIZE; ++scomp) {
		unsigned alive = 0;
		unsigned dcomp;
		unsigned var;

		if (!reg_comp_used(src, scomp))
			continue;

		for (dcomp = 0; dcomp < OF_IR_VEC_SIZE; ++dcomp) {
			const char *mask = (*dst_map)[dcomp];

			if (!reg_comp_used(dst, dcomp)
			    || (dst->deadmask & BIT(dcomp)))
				continue;

			if (mask[scomp] == "xyzw"[scomp]) {
				alive = 1;
				break;
			}
		}

		if (!alive
		    || (src->type == OF_IR_REG_VAR && !src->var[scomp])) {
			src->deadmask |= BIT(scomp);
			continue;
		}
		src->deadmask &= ~BIT(scomp);

		if (src->type != OF_IR_REG_VAR)
			continue;

		alive = of_bitmap_get(opt->live, src->var[scomp]);
		if (alive)
			continue;

		if (opt->want_interference)
			OF_BITMAP_FOR_EACH_SET_BIT(var, opt->live,
						   opt->num_vars)
				add_interference(opt, src->var[scomp], var);

		of_bitmap_set(opt->live, src->var[scomp]);
	}
}

static void
liveness_list(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins, *s;

	LIST_FOR_EACH_ENTRY_SAFE_REV(ins, s, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;
		const struct of_ir_opc_info *info;
		unsigned alive = 0;
		unsigned comp;
		unsigned i;

		if (!dst || dst->type != OF_IR_REG_VAR)
			goto no_dst;

		for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
			uint16_t var = dst->var[comp];
			unsigned comp_alive;

			if (!reg_comp_used(dst, comp))
				continue;

			get_var(opt, var)->def_ins = ins;
			comp_alive = of_bitmap_get(opt->live, var);
			of_bitmap_clear(opt->live, var);
			alive |= comp_alive;

			if (!comp_alive)
				dst->deadmask |= BIT(comp);
			else
				dst->deadmask &= ~BIT(comp);
		}

		if (!alive) {
			ins->flags |= OF_IR_INSTR_DEAD;
			continue;
		}
		ins->flags &= ~OF_IR_INSTR_DEAD;

no_dst:
		info = of_ir_get_opc_info(ins->opc);

		for (i = 0; i < ins->num_srcs; ++i) {
			struct of_ir_register *src = ins->srcs[i];
			const dst_map_t *dst_map = &info->dst_map[i];

			liveness_src(opt, dst, src, dst_map);
		}
	}
}

static void
liveness_phi_dst(struct of_ir_optimizer *opt, struct list_head *phis,
		 unsigned num_srcs)
{
	struct of_ir_phi *phi, *s;

	LIST_FOR_EACH_ENTRY_SAFE_REV(phi, s, phis, list) {
		unsigned var = phi->dst;
		unsigned alive;

		get_var(opt, var)->def_phi = phi;
		alive = of_bitmap_get(opt->live, var);
		of_bitmap_clear(opt->live, var);

		if (!alive) {
			phi->dead = 1;
			continue;
		}
		phi->dead = 0;
	}
}

static void
liveness_phi_src(struct of_ir_optimizer *opt, struct list_head *phis,
		 unsigned src)
{
	struct of_ir_phi *phi, *s;

	LIST_FOR_EACH_ENTRY_SAFE_REV(phi, s, phis, list) {
		unsigned alive;
		unsigned var;

		if (phi->dead || !phi->src[src])
			continue;

		alive = of_bitmap_get(opt->live, phi->src[src]);
		if (alive)
			continue;

		if (opt->want_interference)
			OF_BITMAP_FOR_EACH_SET_BIT(var, opt->live,
						   opt->num_vars)
				add_interference(opt, phi->src[src], var);

		of_bitmap_set(opt->live, phi->src[src]);
	}
}

static void
copy_bitmap(struct of_ir_optimizer *opt, uint32_t **dst_ptr, uint32_t *src,
	    unsigned size)
{
	uint32_t *dst = *dst_ptr;

	if (!dst)
		*dst_ptr = dst = util_slab_alloc(&opt->live_slab);

	if (src)
		of_bitmap_copy(dst, src, size);
	else
		of_bitmap_fill(dst, 0, size);
}

void
liveness(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child, *s;
	struct of_ir_ast_node *region;
	uint32_t *live_copy;

	switch (node->type) {
	case OF_IR_NODE_LIST:
		copy_bitmap(opt, &node->liveout, opt->live, opt->num_vars);
		liveness_list(opt, node);
		copy_bitmap(opt, &node->livein, opt->live, opt->num_vars);
		return;

	case OF_IR_NODE_REGION:
		live_copy = util_slab_alloc(&opt->live_slab);
		copy_bitmap(opt, &live_copy, opt->live, opt->num_vars);

		liveness_phi_dst(opt, &node->ssa.phis,
					node->ssa.depart_count);

		copy_bitmap(opt, &node->liveout, opt->live, opt->num_vars);
		of_bitmap_fill(opt->live, 0, opt->num_vars);
		if (!LIST_IS_EMPTY(&node->ssa.loop_phis) && node->livein)
			of_bitmap_fill(node->livein, 0, opt->num_vars);
		break;

	case OF_IR_NODE_DEPART:
		region = node->depart_repeat.region;
		copy_bitmap(opt, &opt->live, region->liveout, opt->num_vars);
		liveness_phi_src(opt, &region->ssa.phis,
					node->ssa.depart_number);
		break;

	case OF_IR_NODE_REPEAT:
		region = node->depart_repeat.region;
		copy_bitmap(opt, &opt->live, region->livein, opt->num_vars);
		liveness_phi_src(opt, &region->ssa.loop_phis,
					node->ssa.repeat_number);
		break;

	case OF_IR_NODE_IF_THEN:
		copy_bitmap(opt, &node->liveout, opt->live, opt->num_vars);
		break;
	}

	LIST_FOR_EACH_ENTRY_SAFE_REV(child, s, &node->nodes, parent_list)
		liveness(opt, child);

	switch (node->type) {
	case OF_IR_NODE_REGION:
		if (!LIST_IS_EMPTY(&node->ssa.loop_phis)) {
			liveness_phi_dst(opt, &node->ssa.loop_phis,
						node->ssa.repeat_count + 1);

			copy_bitmap(opt, &node->livein, opt->live, opt->num_vars);

			LIST_FOR_EACH_ENTRY_SAFE_REV(child, s, &node->nodes,
						     parent_list)
				liveness(opt, child);

			liveness_phi_dst(opt, &node->ssa.loop_phis,
						node->ssa.repeat_count + 1);
			liveness_phi_src(opt, &node->ssa.loop_phis, 0);
		}

		copy_bitmap(opt, &node->liveout, live_copy, opt->num_vars);
		copy_bitmap(opt, &node->livein, opt->live, opt->num_vars);
		util_slab_free(&opt->live_slab, live_copy);
		break;

	case OF_IR_NODE_IF_THEN:
		of_bitmap_or(opt->live, opt->live, node->liveout, opt->num_vars);
		break;

	default:
		break;
	}
}

/*
 * Dead code elimination, including masking out unused vector componenents.
 */

static void
cleanup_list(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins, *s;

	LIST_FOR_EACH_ENTRY_SAFE(ins, s, &node->list.instrs, list) {
		struct of_ir_register *dst = ins->dst;
		unsigned i;

		if (ins->flags & OF_IR_INSTR_DEAD) {
			list_del(&ins->list);
			continue;
		}

		if (dst)
			dst->mask &= ~dst->deadmask;

		for (i = 0; i < ins->num_srcs; ++i) {
			struct of_ir_register *src = ins->srcs[i];

			src->mask &= ~src->deadmask;
		}
	}
}

static void
cleanup_phis(struct of_ir_optimizer *opt, struct list_head *phis)
{
	struct of_ir_phi *phi, *s;

	LIST_FOR_EACH_ENTRY_SAFE(phi, s, phis, list)
		if (phi->dead)
			list_del(&phi->list);
}

void
cleanup(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child, *s;

	/* Clean-up live bitmap pointers. Memory is pooled using a slab cache,
	 * so all bitmaps will be freed in one go later. */
	node->livein = NULL;
	node->liveout = NULL;

	if (node->type == OF_IR_NODE_LIST)
		return cleanup_list(opt, node);

	LIST_FOR_EACH_ENTRY_SAFE(child, s, &node->nodes, parent_list)
		cleanup(opt, child);

	if (!LIST_IS_EMPTY(&node->ssa.phis))
		cleanup_phis(opt, &node->ssa.phis);
	if (!LIST_IS_EMPTY(&node->ssa.loop_phis))
		cleanup_phis(opt, &node->ssa.loop_phis);
}

/*
 * Validation pass. Enforces hardware constraints not related to temporary
 * variables.
 *
 * TODO: Analyse usage of non-temporary registers and preload those that
 * can be reused further in the code. Also try to not preload the same
 * register multiple times.
 */

static void
assign_to_tmp(struct of_ir_optimizer *opt, struct of_ir_instruction *ins,
	      struct of_ir_register *reg)
{
	struct of_ir_register *dst, *src;
	struct of_ir_instruction *copy;
	unsigned comp;

	copy = of_ir_instr_create(opt->shader, OF_OP_MOV);
	dst = of_ir_reg_create(opt->shader, OF_IR_REG_VAR, 0, "xyzw", 0);
	of_ir_instr_add_dst(copy, dst);
	src = of_ir_reg_clone(opt->shader, reg);
	of_ir_instr_add_src(copy, src);

	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
		if (!reg_comp_used(reg, comp))
			continue;

		dst->var[comp] = reg->var[comp] = add_var_num(opt);
	}

	dst->mask = reg->mask;
	src->mask = reg->mask;
	src->flags = 0;
	reg->type = OF_IR_REG_VAR;

	of_ir_instr_insert_before(opt->shader, NULL, ins, copy);
}

static void
validate_list(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;
	uint8_t reg_count[OF_IR_NUM_REG_TYPES];

	LIST_FOR_EACH_ENTRY(ins, &node->list.instrs, list) {
		unsigned i;

		memset(reg_count, 0, sizeof(reg_count));

		for (i = 0; i < ins->num_srcs; ++i) {
			struct of_ir_register *src = ins->srcs[i];

			if (src->type == OF_IR_REG_VAR)
				continue;

			if (++reg_count[src->type] > 1)
				assign_to_tmp(opt, ins, src);
		}
	}
}

static void
validate(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	if (node->type == OF_IR_NODE_LIST)
		return validate_list(opt, node);

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		validate(opt, child);
}

/*
 * Forward propagation of MOV sources.
 *
 * This pass tries to eliminate any register moves in the code by tracking
 * contents of variables affected by them and replacing, if possible, source
 * operands of instructions accessing them with sources of respective move.
 *
 * For non-temp (inputs, constants) to temp (variables) moves, due to HW
 * design, source registers and flags must be the same for all components of
 * the vector. For temp to temp moves, since registers are not assigned yet,
 * we require just the flags to match. In addition, flags of MOV source may
 * be different than those of affected instructions, because they can be
 * merged together forming new set of flags.
 */

struct of_ir_var_map {
	enum of_ir_reg_flags flags;
	enum of_ir_reg_type type;
	uint16_t reg;
	uint8_t comp;
};

static INLINE void
make_map(struct of_ir_var_map *map, struct of_ir_register *reg, unsigned comp)
{
	map->type = reg->type;
	map->flags = reg->flags;

	if (reg->type == OF_IR_REG_VAR) {
		map->reg = reg->var[comp];
		map->comp = comp;
	} else {
		map->reg = reg->num;
		map->comp = reg->swizzle[comp];
	}
}

static bool
maps_compatible(struct of_ir_var_map *m1, struct of_ir_var_map *m2)
{
	if (!m2->type)
		return false;
	if (!m1)
		return true;
	if (m1->type != m2->type)
		return false;
	if (m1->type != OF_IR_REG_VAR && m1->reg != m2->reg)
		return false;
	return m1->flags == m2->flags;
}

static void
src_propagation_list(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;

	LIST_FOR_EACH_ENTRY(ins, &node->list.instrs, list) {
		struct of_ir_register *dst, *src;
		unsigned comp;
		uint16_t var;
		unsigned i;

		for (i = 0; i < ins->num_srcs; ++i) {
			struct of_ir_var_map *map = NULL;

			src = ins->srcs[i];
			if (src->type != OF_IR_REG_VAR)
				continue;

			for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
				struct of_ir_var_map *map_next;

				var = src->var[comp];
				if (!reg_comp_used(src, comp) || !var)
					continue;

				map_next = &opt->maps[var];
				if (!maps_compatible(map, map_next))
					break;

				map = map_next;
			}
			if (comp != OF_IR_VEC_SIZE)
				continue;

			for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
				var = src->var[comp];
				if (!reg_comp_used(src, comp) || !var)
					continue;

				map = &opt->maps[var];
				src->swizzle[comp] = map->comp;
				src->var[comp] = map->reg;
			}

			src->num = map->reg;
			src->type = map->type;
			of_ir_merge_flags(src, map->flags);
		}

		dst = ins->dst;
		src = ins->srcs[0];

		if (ins->opc != OF_OP_MOV
		    || dst->type != OF_IR_REG_VAR || dst->flags)
			continue;

		for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
			uint16_t dstv = dst->var[comp];

			if (!reg_comp_used(dst, comp))
				continue;

			make_map(&opt->maps[dstv], src, comp);
		}
	}
}

static INLINE void
src_propagation_phi(struct of_ir_optimizer *opt, unsigned src,
		    struct of_ir_phi *phi)
{
	uint16_t var = phi->src[src];
	struct of_ir_var_map *map = &opt->maps[var];

	if (map->type != OF_IR_REG_VAR || map->flags)
		return;

	phi->src[src] = map->reg;
}

static void
src_propagation(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *region;
	struct of_ir_ast_node *child;
	struct of_ir_phi *phi;

	switch (node->type) {
	case OF_IR_NODE_REGION:
		LIST_FOR_EACH_ENTRY(phi, &node->ssa.loop_phis, list)
			src_propagation_phi(opt, 0, phi);
		break;

	case OF_IR_NODE_IF_THEN:
		LIST_FOR_EACH_ENTRY(phi, &node->ssa.phis, list)
			src_propagation_phi(opt, 0, phi);
		break;

	case OF_IR_NODE_DEPART:
	case OF_IR_NODE_REPEAT:
		opt->maps = of_stack_push_copy(opt->maps_stack);
		break;

	case OF_IR_NODE_LIST:
		src_propagation_list(opt, node);
		return;
	}

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		src_propagation(opt, child);

	switch (node->type) {
	case OF_IR_NODE_REGION:
		break;

	case OF_IR_NODE_IF_THEN:
		LIST_FOR_EACH_ENTRY(phi, &node->ssa.phis, list)
			src_propagation_phi(opt, 1, phi);
		break;

	case OF_IR_NODE_DEPART:
		region = node->depart_repeat.region;
		LIST_FOR_EACH_ENTRY(phi, &region->ssa.phis, list)
			src_propagation_phi(opt, node->ssa.depart_number, phi);

		opt->maps = of_stack_pop(opt->maps_stack);
		break;

	case OF_IR_NODE_REPEAT:
		region = node->depart_repeat.region;
		LIST_FOR_EACH_ENTRY(phi, &region->ssa.loop_phis, list)
			src_propagation_phi(opt, node->ssa.repeat_number, phi);

		opt->maps = of_stack_pop(opt->maps_stack);
		break;

	default:
		break;
	}
}

/*
 * Optimize-specific data dumping.
 * TODO: Probably some code could be shared with register allocation.
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

/*
 * Optimizer entry point.
 * NOTE: Expects the IR to be already in SSA form. Leaves the IR in SSA form.
 */

int
of_ir_optimize(struct of_ir_shader *shader)
{
	struct of_ir_optimizer *opt;
	struct of_heap *heap;

	heap = of_heap_create();
	opt = of_heap_alloc(heap, sizeof(*opt));
	opt->shader = shader;
	opt->heap = heap;
	opt->num_vars = shader->stats.num_vars;
	util_slab_create(&opt->live_slab, OF_BITMAP_BYTES_FOR_BITS(opt->num_vars),
				32, UTIL_SLAB_SINGLETHREADED);
	opt->live = util_slab_alloc(&opt->live_slab);
	of_bitmap_fill(opt->live, 0, opt->num_vars);
	util_dynarray_init(&opt->vars);
	util_dynarray_resize(&opt->vars, opt->num_vars
				* sizeof(struct of_ir_variable));
	memset(util_dynarray_begin(&opt->vars), 0, opt->num_vars
				* sizeof(struct of_ir_variable));

	opt->maps_stack = of_stack_create(opt->num_vars
						* sizeof(*opt->maps), 1);
	opt->maps = of_stack_top(opt->maps_stack);
	memset(opt->maps, 0, opt->num_vars * sizeof(*opt->maps));
	RUN_PASS(shader, opt, src_propagation);
	of_stack_destroy(opt->maps_stack);

	DBG("AST (post-src-propagation)");
	of_ir_dump_ast(shader, dump_opt_data, opt);

	RUN_PASS(shader, opt, liveness);
	RUN_PASS(shader, opt, cleanup);
	RUN_PASS(shader, opt, validate);

	DBG("AST (post-optimize/pre-register-assignment)");
	of_ir_dump_ast(shader, dump_opt_data, opt);

	shader->stats.num_vars = opt->num_vars;
	util_dynarray_fini(&opt->vars);
	util_slab_destroy(&opt->live_slab);
	of_heap_destroy(heap);

	return 0;
}
