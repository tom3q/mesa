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

		if (!(src->mask & (1 << scomp)))
			continue;

		for (dcomp = 0; dcomp < OF_IR_VEC_SIZE; ++dcomp) {
			const char *mask = (*dst_map)[dcomp];

			if (!(dst->mask & (1 << dcomp))
			    || (dst->deadmask & (1 << dcomp)))
				continue;

			if (mask[scomp] == "xyzw"[scomp]) {
				alive = 1;
				break;
			}
		}

		if (!alive) {
			src->deadmask |= 1 << scomp;
			continue;
		}
		src->deadmask &= ~(1 << scomp);

		if (src->type != OF_IR_REG_VAR)
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

			if (!(dst->mask & (1 << comp)))
				continue;

			get_var(opt, var)->def_ins = ins;
			comp_alive = of_bitmap_get(opt->live, var);
			of_bitmap_clear(opt->live, var);
			alive |= comp_alive;

			if (!comp_alive)
				dst->deadmask |= 1 << comp;
			else
				dst->deadmask &= ~(1 << comp);
		}

		if (!alive) {
			ins->flags |= OF_IR_INSTR_DEAD;
			continue;
		}
		ins->flags &= ~OF_IR_INSTR_DEAD;

no_dst:
		info = of_ir_get_opc_info(ins->opc);

		for (i = 0; i < OF_IR_NUM_SRCS && ins->srcs[i]; ++i) {
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
		unsigned var;

		if (phi->dead)
			continue;

		of_bitmap_set(opt->live, phi->src[src]);

		if (opt->want_interference)
			OF_BITMAP_FOR_EACH_SET_BIT(var, opt->live,
						   opt->num_vars)
				add_interference(opt, phi->src[src], var);
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
	struct of_ir_optimizer *opt;
	struct of_heap *heap;

	heap = of_heap_create();
	opt = of_heap_alloc(heap, sizeof(*opt));
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

	RUN_PASS(shader, opt, liveness);
	RUN_PASS(shader, opt, cleanup);

	DBG("AST (post-optimize/pre-register-assignment)");
	of_ir_dump_ast(shader, dump_opt_data, opt);

	util_dynarray_fini(&opt->vars);
	util_slab_destroy(&opt->live_slab);
	of_heap_destroy(heap);

	return 0;
}
