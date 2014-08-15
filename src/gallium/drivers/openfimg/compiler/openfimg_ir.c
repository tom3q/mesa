/*
 * Copyright (C) 2013-2014 Tomasz Figa <tomasz.figa@gmail.com>
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

#include "openfimg_ir_priv.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "openfimg_program.h"
#include "openfimg_util.h"

#include "fimg_3dse.xml.h"

#define OF_IR_OPC(_opc, _type, _num_srcs)	\
	[OF_OP_ ## _opc] = {			\
		.name = #_opc,			\
		.type = OF_IR_ ## _type,	\
		.num_srcs = _num_srcs,		\
		.dst_map = vector_dst_map,	\
	}

#define OF_IR_OPC_MAP_REP(_opc, _type, _num_srcs, _map)	\
	[OF_OP_ ## _opc] = {				\
		.name = #_opc,				\
		.type = OF_IR_ ## _type,		\
		.num_srcs = _num_srcs,			\
		.dst_map = _map,			\
		.replicated = true,			\
	}

#define OF_IR_OPC_MAP_FIX(_opc, _type, _num_srcs, _map)	\
	[OF_OP_ ## _opc] = {				\
		.name = #_opc,				\
		.type = OF_IR_ ## _type,		\
		.num_srcs = _num_srcs,			\
		.dst_map = _map,			\
		.fix_comp = true,			\
	}

#define OF_IR_OPC_MAP_TEX(_opc, _type, _num_srcs)	\
	[OF_OP_ ## _opc] = {				\
		.name = #_opc,				\
		.type = OF_IR_ ## _type,		\
		.num_srcs = _num_srcs,			\
		.dst_map = full_dst_map,		\
		.tex = true,			\
	}

static const dst_map_t vector_dst_map[] = {
	{ "x___", "_y__", "__z_", "___w" },
	{ "x___", "_y__", "__z_", "___w" },
	{ "x___", "_y__", "__z_", "___w" },
};

static const dst_map_t dp3_dst_map[] = {
	{ "xyz_", "xyz_", "xyz_", "xyz_" },
	{ "xyz_", "xyz_", "xyz_", "xyz_" },
};

static const dst_map_t dp4_dst_map[] = {
	{ "xyzw", "xyzw", "xyzw", "xyzw" },
	{ "xyzw", "xyzw", "xyzw", "xyzw" },
};

static const dst_map_t dph_dst_map[] = {
	{ "xyz_", "xyz_", "xyz_", "xyz_" },
	{ "xyzw", "xyzw", "xyzw", "xyzw" },
};

static const dst_map_t dst_dst_map[] = {
	{ "____", "_y__", "__z_", "____" },
	{ "____", "_y__", "____", "___w" },
};

static const dst_map_t scalar_dst_map[] = {
	{ "x___", "x___", "x___", "x___" },
};

static const dst_map_t dp2add_dst_map[] = {
	{ "xy__", "xy__", "xy__", "xy__" },
	{ "xy__", "xy__", "xy__", "xy__" },
	{ "x___", "x___", "x___", "x___" },
};

static const dst_map_t full_dst_map[] = {
	{ "xyzw", "xyzw", "xyzw", "xyzw" },
	{ "xyzw", "xyzw", "xyzw", "xyzw" },
	{ "xyzw", "xyzw", "xyzw", "xyzw" },
};

const struct of_ir_opc_info of_ir_opc_info[] = {
	OF_IR_OPC(NOP, SUB, 0), /* Not really SUB, but emitted the same way. */
	OF_IR_OPC(MOV, ALU, 1),
	OF_IR_OPC(MOVA, ALU, 1),
	OF_IR_OPC(MOVC, ALU, 2),
	OF_IR_OPC(ADD, ALU, 2),
	OF_IR_OPC(MUL, ALU, 2),
	OF_IR_OPC(MUL_LIT, ALU, 2),
	OF_IR_OPC_MAP_REP(DP3, ALU, 2, dp3_dst_map),
	OF_IR_OPC_MAP_REP(DP4, ALU, 2, dp4_dst_map),
	OF_IR_OPC_MAP_REP(DPH, ALU, 2, dph_dst_map),
	OF_IR_OPC_MAP_FIX(DST, ALU, 2, dst_dst_map),
	OF_IR_OPC_MAP_REP(EXP, ALU, 1, scalar_dst_map),
	OF_IR_OPC_MAP_REP(EXP_LIT, ALU, 1, scalar_dst_map),
	OF_IR_OPC_MAP_REP(LOG, ALU, 1, scalar_dst_map),
	OF_IR_OPC_MAP_REP(LOG_LIT, ALU, 1, scalar_dst_map),
	OF_IR_OPC_MAP_REP(RCP, ALU, 1, scalar_dst_map),
	OF_IR_OPC_MAP_REP(RSQ, ALU, 1, scalar_dst_map),
	OF_IR_OPC_MAP_REP(DP2ADD, ALU, 3, dp2add_dst_map),
	OF_IR_OPC(MAX, ALU, 2),
	OF_IR_OPC(MIN, ALU, 2),
	OF_IR_OPC(SGE, ALU, 2),
	OF_IR_OPC(SLT, ALU, 2),
	OF_IR_OPC(SETP_EQ, ALU, 2),
	OF_IR_OPC(SETP_GE, ALU, 2),
	OF_IR_OPC(SETP_GT, ALU, 2),
	OF_IR_OPC(SETP_NE, ALU, 2),
	OF_IR_OPC(CMP, ALU, 3),
	OF_IR_OPC(MAD, ALU, 3),
	OF_IR_OPC(FRC, ALU, 1),
	OF_IR_OPC(FLR, ALU, 1),
	OF_IR_OPC_MAP_TEX(TEXLD, ALU, 2),
	OF_IR_OPC_MAP_FIX(CUBEDIR, ALU, 1, full_dst_map),
	OF_IR_OPC_MAP_REP(MAXCOMP, ALU, 1, full_dst_map),
	OF_IR_OPC_MAP_TEX(TEXLDC, ALU, 3),
	OF_IR_OPC_MAP_REP(TEXKILL, ALU, 1, full_dst_map),
	OF_IR_OPC(MOVIPS, ALU, 1),
	OF_IR_OPC(ADDI, ALU, 2),
	OF_IR_OPC(B, CF, 0),
	OF_IR_OPC(BF, CF, 1),
	OF_IR_OPC(BP, CF, 0),
	OF_IR_OPC(BFP, CF, 1),
	OF_IR_OPC(BZP, CF, 1),
	OF_IR_OPC(CALL, SUB, 0),
	OF_IR_OPC(CALLNZ, SUB, 1),
	OF_IR_OPC(RET, SUB, 0),
};

#define OF_IR_REG_RW(_reg, _num_regs, _a0_addr, _al_addr, _num_reads)	\
	[OF_IR_REG_ ## _reg] = {					\
		.name = #_reg,						\
		.src_type = OF_SRC_ ## _reg,				\
		.dst_type = OF_DST_ ## _reg,				\
		.a0_addr = _a0_addr,					\
		.al_addr = _al_addr,					\
		.num_reads = _num_reads,				\
		.writable = true,					\
		.readable = true,					\
	}

#define OF_IR_REG_R(_reg, _num_regs, _a0_addr, _al_addr, _num_reads)	\
	[OF_IR_REG_ ## _reg] = {					\
		.name = #_reg,						\
		.src_type = OF_SRC_ ## _reg,				\
		.a0_addr = _a0_addr,					\
		.al_addr = _al_addr,					\
		.num_reads = _num_reads,				\
		.readable = true,					\
	}

#define OF_IR_REG_W(_reg, _num_regs, _a0_addr, _al_addr)		\
	[OF_IR_REG_ ## _reg] = {					\
		.name = #_reg,						\
		.dst_type = OF_DST_ ## _reg,				\
		.a0_addr = _a0_addr,					\
		.al_addr = _al_addr,					\
		.writable = true,					\
	}

#define OF_IR_REG_VAR_INFO						\
	[OF_IR_REG_VAR] = {						\
		.name = "@",						\
		.writable = true,					\
		.readable = true,					\
	}

#define OF_IR_REG_VARC_INFO						\
	[OF_IR_REG_VARC] = {						\
		.name = "$",						\
		.writable = true,					\
		.readable = true,					\
	}

static const struct of_ir_reg_info vs_reg_info[OF_IR_NUM_REG_TYPES] = {
	/* (reg, num_regs, a0_addr, al_addr[, num_reads]) */
	OF_IR_REG_RW(R,   32, false, true,  2),
	OF_IR_REG_R( V,   10, false, true,  1),
	OF_IR_REG_R( C,  256, true,  true,  1),
	OF_IR_REG_R( I,   16, false, false, 1),
	OF_IR_REG_RW(AL,   4, false, false, 1),
	OF_IR_REG_R( B,   16, false, false, 1),
	OF_IR_REG_RW(P,    7, false, false, 1),
	OF_IR_REG_R( S,    4, false, false, 1),
	OF_IR_REG_W( O,   10, false, true),
	OF_IR_REG_W(A0,    1, false, false),
	OF_IR_REG_VAR_INFO, /* Virtual variable */
	OF_IR_REG_VARC_INFO, /* Virtual variable */
};

static const struct of_ir_reg_info ps_reg_info[OF_IR_NUM_REG_TYPES] = {
	/* (reg, num_regs, a0_addr, al_addr[, num_reads]) */
	OF_IR_REG_RW(R,    32, false, true,  2),
	OF_IR_REG_R( V,     8, false, true,  1),
	OF_IR_REG_R( C,   256, true,  true,  1),
	OF_IR_REG_R( I,    16, false, false, 1),
	OF_IR_REG_RW(AL,    4, false, false, 1),
	OF_IR_REG_R( B,    16, false, false, 1),
	OF_IR_REG_RW(P,     7, false, false, 1),
	OF_IR_REG_R( S,     8, false, false, 1),
	OF_IR_REG_R( D,     8, false, false, 1),
	OF_IR_REG_R( VFACE, 4, false, false, 1),
	OF_IR_REG_R( VPOS,  4, false, false, 1),
	OF_IR_REG_W( O,     1, false, true),
	OF_IR_REG_W(A0,     1, false, false),
	OF_IR_REG_VAR_INFO, /* Virtual variable */
	OF_IR_REG_VARC_INFO, /* Virtual variable */
};

const struct of_ir_reg_info *
of_ir_get_reg_info(struct of_ir_shader *shader, enum of_ir_reg_type reg)
{
	return &shader->reg_info[reg];
}

/*
 * Register-level operations.
 */

struct of_ir_register *
of_ir_reg_create(struct of_ir_shader *shader, enum of_ir_reg_type type,
		 unsigned num, const char *swizzle, unsigned flags)
{
	struct of_ir_register *reg = of_heap_alloc(shader->heap, sizeof(*reg));
	unsigned comp;

	DEBUG_MSG("%x, %d, %s", flags, num, swizzle);
	reg->flags = flags;
	reg->type = type;
	reg->num = num;
	reg->mask = 0xf;

	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
		switch (swizzle[comp]) {
		case 'x':
			reg->swizzle[comp] = 0;
			break;
		case 'y':
			reg->swizzle[comp] = 1;
			break;
		case 'z':
			reg->swizzle[comp] = 2;
			break;
		case 'w':
			reg->swizzle[comp] = 3;
			break;
		case '_':
			reg->swizzle[comp] = comp;
			reg->mask &= ~BIT(comp);
			break;
		default:
			ERROR_MSG("invalid vector swizzle/mask: %s", swizzle);
			assert(0);
		}

		if (reg->type == OF_IR_REG_VAR)
			reg->var[comp] = OF_IR_VEC_SIZE * num
						+ reg->swizzle[comp];
	}

	return reg;
}

struct of_ir_register *
of_ir_reg_clone(struct of_ir_shader *shader, struct of_ir_register *src)
{
	struct of_ir_register *reg = of_heap_alloc(shader->heap, sizeof(*reg));

	memcpy(reg, src, sizeof(*reg));

	return reg;
}

/*
 * instruction-level operations.
 */

struct of_ir_instruction *
of_ir_instr_create(struct of_ir_shader *shader, enum of_instr_opcode opc)
{
	struct of_ir_instruction *instr;

	instr = of_heap_alloc(shader->heap, sizeof(*instr));

	DEBUG_MSG("%d", opc);
	instr->opc = opc;
	LIST_INITHEAD(&instr->list);

	return instr;
}

void
of_ir_instr_add_dst(struct of_ir_instruction *instr, struct of_ir_register *reg)
{
	unsigned comp;

	assert(!instr->dst);
	instr->dst = reg;

	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp)
		reg->swizzle[comp] = comp;
}

void
of_ir_instr_add_src(struct of_ir_instruction *instr, struct of_ir_register *reg)
{
	assert(instr->num_srcs < ARRAY_SIZE(instr->srcs));
	instr->srcs[instr->num_srcs++] = reg;
	reg->mask = 0xf;
}

static void
insert_instr(struct of_ir_shader *shader, struct of_ir_ast_node *node,
	     struct list_head *list, struct of_ir_instruction *instr)
{
	const struct of_ir_opc_info *info = of_ir_get_opc_info(instr->opc);

	assert(instr->num_srcs == info->num_srcs);

	if (instr->dst && instr->dst->type == OF_IR_REG_VAR) {
		unsigned comp;

		for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
			if (!(instr->dst->mask & (1 << comp)))
				continue;

			shader->stats.num_vars =
					max(shader->stats.num_vars,
						instr->dst->var[comp] + 1);
		}
	}

	list_addtail(&instr->list, list);
	instr->node = node;
}

void
of_ir_instr_insert_before(struct of_ir_shader *shader,
			  struct of_ir_ast_node *node,
			  struct of_ir_instruction *where,
			  struct of_ir_instruction *instr)
{
	struct list_head *list;

	assert(node || where);

	if (where) {
		/* Explicitly specified instruction to prepend. */
		node = where->node;
		list = &where->list;
	} else {
		/* Explicitly specified list node. */
		assert(node->type == OF_IR_NODE_LIST);
		list = node->list.instrs.next;
	}

	insert_instr(shader, node, list, instr);
}

void
of_ir_instr_insert(struct of_ir_shader *shader, struct of_ir_ast_node *node,
		   struct of_ir_instruction *where,
		   struct of_ir_instruction *instr)
{
	struct list_head *list;

	assert(node || where);

	if (where) {
		/* Explicitly specified instruction to append. */
		node = where->node;
		list = where->list.next;
	} else {
		/* Explicitly specified list node. */
		assert(node->type == OF_IR_NODE_LIST);
		list = &node->list.instrs;
	}

	insert_instr(shader, node, list, instr);
}

static void
merge_mask(struct of_ir_register *reg, const char *mask)
{
	unsigned comp;

	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp)
		if (mask[comp] != "xyzw"[comp])
			reg->mask &= ~BIT(comp);
}

static void
merge_swizzle(struct of_ir_register *reg, const char *swizzle)
{
	unsigned comp;
	char result[4];

	for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
		switch (swizzle[comp]) {
		case 'x': result[comp] = reg->swizzle[0]; break;
		case 'y': result[comp] = reg->swizzle[1]; break;
		case 'z': result[comp] = reg->swizzle[2]; break;
		case 'w': result[comp] = reg->swizzle[3]; break;
		default:
			ERROR_MSG("invalid vector src swizzle: %s", swizzle);
			assert(0);
		}
	}

	memcpy(reg->swizzle, result, sizeof(reg->swizzle));
}

static void
merge_flags(struct of_ir_register *reg, enum of_ir_reg_flags flags)
{
	/* Exclusive flags */
	if (reg->flags & OF_IR_REG_NEGATE && flags & OF_IR_REG_ABS)
		reg->flags &= ~OF_IR_REG_NEGATE;

	/* Additive flags */
	reg->flags |= flags;
}

void
of_ir_instr_insert_templ(struct of_ir_shader *shader,
			 struct of_ir_ast_node *node,
			 struct of_ir_instruction *where,
			 const struct of_ir_instr_template *instrs,
			 unsigned num_instrs)
{
	struct of_ir_instruction *instr;
	unsigned src;

	while (num_instrs--) {
		instr = of_ir_instr_create(shader, instrs->opc);

		if (instrs->dst.reg) {
			if (instrs->dst.mask)
				merge_mask(instrs->dst.reg, instrs->dst.mask);

			merge_flags(instrs->dst.reg, instrs->dst.flags);
			of_ir_instr_add_dst(instr, instrs->dst.reg);
		}

		for (src = 0; src < OF_IR_NUM_SRCS; ++src) {
			if (!instrs->src[src].reg)
				break;

			if (instrs->src[src].swizzle)
				merge_swizzle(instrs->src[src].reg,
						instrs->src[src].swizzle);

			merge_flags(instrs->src[src].reg,
					instrs->src[src].flags);
			of_ir_instr_add_src(instr, instrs->src[src].reg);
		}

		of_ir_instr_insert(shader, node, where, instr);
		++instrs;
	}
}

/*
 * AST node-level operations.
 */

struct of_ir_ast_node *
of_ir_node_region(struct of_ir_shader *shader)
{
	struct of_ir_ast_node *node;

	node = of_heap_alloc(shader->heap, sizeof(*node));
	node->shader = shader;

	LIST_INITHEAD(&node->nodes);

	list_addtail(&node->parent_list, &shader->root_nodes);

	return node;
}

struct of_ir_ast_node *
of_ir_node_if_then(struct of_ir_shader *shader, struct of_ir_register *reg,
		 const char *swizzle, unsigned flags)
{
	struct of_ir_ast_node *node = of_ir_node_region(shader);

	node->type = OF_IR_NODE_IF_THEN;
	node->if_then.reg = reg;
	merge_swizzle(reg, swizzle);
	merge_flags(reg, flags);

	return node;
}

struct of_ir_ast_node *
of_ir_node_depart(struct of_ir_shader *shader, struct of_ir_ast_node *region)
{
	struct of_ir_ast_node *node = of_ir_node_region(shader);

	node->type = OF_IR_NODE_DEPART;
	node->depart_repeat.region = region;

	return node;
}

struct of_ir_ast_node *
of_ir_node_repeat(struct of_ir_shader *shader, struct of_ir_ast_node *region)
{
	struct of_ir_ast_node *node = of_ir_node_region(shader);

	node->type = OF_IR_NODE_REPEAT;
	node->depart_repeat.region = region;

	return node;
}

struct of_ir_ast_node *
of_ir_node_list(struct of_ir_shader *shader)
{
	struct of_ir_ast_node *node = of_ir_node_region(shader);

	node->type = OF_IR_NODE_LIST;
	LIST_INITHEAD(&node->list.instrs);

	return node;
}

static void
insert_node(struct of_ir_ast_node *node, struct list_head *where,
	    struct of_ir_ast_node *parent)
{
	list_del(&node->parent_list);
	list_addtail(&node->parent_list, where);
	node->parent = parent;
}

void
of_ir_node_insert(struct of_ir_ast_node *where, struct of_ir_ast_node *node)
{
	insert_node(node, &where->nodes, where);
}

enum of_ir_node_type of_ir_node_get_type(struct of_ir_ast_node *node)
{
	return node->type;
}

struct of_ir_ast_node *
of_ir_node_get_parent(struct of_ir_ast_node *node)
{
	return node->parent;
}

struct of_ir_ast_node *
of_ir_node_list_before(struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *list;

	if (!node->parent)
		return NULL;

	if (node->parent_list.prev != &node->parent->nodes) {
		struct of_ir_ast_node *prev;

		prev = LIST_ENTRY(struct of_ir_ast_node, node->parent_list.prev,
					parent_list);
		if (prev->type == OF_IR_NODE_LIST)
			return prev;
	}

	list = of_ir_node_list(node->shader);
	insert_node(list, &node->parent_list, node->parent);

	return list;
}

struct of_ir_ast_node *
of_ir_node_list_after(struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *list;

	if (!node->parent)
		return NULL;

	if (node->parent_list.next != &node->parent->nodes) {
		struct of_ir_ast_node *next;

		next = LIST_ENTRY(struct of_ir_ast_node, node->parent_list.next,
					parent_list);
		if (next->type == OF_IR_NODE_LIST)
			return next;
	}

	list = of_ir_node_list(node->shader);
	insert_node(list, node->parent_list.next, node->parent);

	return list;
}

struct of_ir_ast_node *
of_ir_node_list_front(struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *list;

	if (!LIST_IS_EMPTY(&node->nodes)) {
		struct of_ir_ast_node *first;

		first = LIST_ENTRY(struct of_ir_ast_node, node->nodes.next,
					parent_list);
		if (first->type == OF_IR_NODE_LIST)
			return first;
	}

	list = of_ir_node_list(node->shader);
	insert_node(list, node->nodes.next, node);

	return list;
}

struct of_ir_ast_node *
of_ir_node_list_back(struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *list;

	if (!LIST_IS_EMPTY(&node->nodes)) {
		struct of_ir_ast_node *last;

		last = LIST_ENTRY(struct of_ir_ast_node, node->nodes.prev,
					parent_list);
		if (last->type == OF_IR_NODE_LIST)
			return last;
	}

	list = of_ir_node_list(node->shader);
	insert_node(list, &node->nodes, node);

	return list;
}

/*
 * Shader-level operations.
 */

struct of_ir_shader *
of_ir_shader_create(enum of_shader_type type)
{
	struct of_ir_shader *shader;

	DEBUG_MSG("");
	shader = CALLOC_STRUCT(of_ir_shader);
	if (!shader)
		return NULL;

	shader->heap = of_heap_create();

	LIST_INITHEAD(&shader->root_nodes);

	if (type == OF_SHADER_VERTEX)
		shader->reg_info = vs_reg_info;
	else if (type == OF_SHADER_PIXEL)
		shader->reg_info = ps_reg_info;
	else
		assert(0);

	return shader;
}

void
of_ir_shader_destroy(struct of_ir_shader *shader)
{
	if (!shader)
		return;

	DEBUG_MSG("");
	of_heap_destroy(shader->heap);
	FREE(shader);
}

/*
 * AST cleaner
 */

static void
depart_region(struct of_ir_shader *shader, struct of_ir_ast_node *region)
{
	struct of_ir_ast_node *child, *s;
	struct of_ir_ast_node *depart;
	struct list_head tmp_list;

	list_inithead(&tmp_list);

	LIST_FOR_EACH_ENTRY_SAFE(child, s, &region->nodes, parent_list) {
		list_del(&child->parent_list);
		list_addtail(&child->parent_list, &tmp_list);
	}

	depart = of_ir_node_depart(shader, region);

	LIST_FOR_EACH_ENTRY_SAFE(child, s, &tmp_list, parent_list)
		of_ir_node_insert(depart, child);

	of_ir_node_insert(region, depart);
}

static void
clean_node(struct of_ir_shader *shader, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child, *s;

	switch (node->type) {
	case OF_IR_NODE_LIST:
		if (LIST_IS_EMPTY(&node->list.instrs))
			LIST_DELINIT(&node->parent_list);
		return;
	default:
		break;
	}

	LIST_FOR_EACH_ENTRY_SAFE(child, s, &node->nodes, parent_list)
		clean_node(shader, child);

	switch (node->type) {
	case OF_IR_NODE_REGION:
		if (LIST_IS_EMPTY(&node->nodes))
			break;
		child = LIST_ENTRY(struct of_ir_ast_node,
					node->nodes.prev, parent_list);
		if (child->type != OF_IR_NODE_DEPART
		    && child->type != OF_IR_NODE_REPEAT)
			depart_region(shader, node);
		break;
	default:
		break;
	}
}

static void
clean_ast(struct of_ir_shader *shader)
{
	struct of_ir_ast_node *node;

	LIST_FOR_EACH_ENTRY(node, &shader->root_nodes, parent_list)
		clean_node(shader, node);
}

/*
 * AST dumper
 */

static void
lsnprintf(char **buf, size_t *maxlen, const char *format, ...)
{
	va_list ap;
	size_t len;

	va_start(ap, format);
	len = vsnprintf(*buf, *maxlen, format, ap);
	va_end(ap);

	if (len + 1 > *maxlen)
		len = *maxlen - 1;

	*maxlen -= len;
	*buf += len;
}

static void
format_src_reg(struct of_ir_shader *shader, char *buf, size_t maxlen,
	       struct of_ir_register *reg)
{
	const struct of_ir_reg_info *info;
	unsigned comp;

	info = of_ir_get_reg_info(shader, reg->type);

	lsnprintf(&buf, &maxlen, "%s%s",
			reg->flags & OF_IR_REG_NEGATE ? "-" : " ",
			reg->flags & OF_IR_REG_ABS ? "|" : "[");

	comp = 0;
	do {
		if (!(reg->mask & (1 << comp)))
			lsnprintf(&buf, &maxlen, "_______");
		else if (reg->type == OF_IR_REG_VAR
			   || reg->type == OF_IR_REG_VARC)
			lsnprintf(&buf, &maxlen, "%2s%03d  ", info->name,
					reg->var[comp]);
		else
			lsnprintf(&buf, &maxlen, "%2s%03d.%c", info->name,
					reg->num, "xyzw"[reg->swizzle[comp]]);

		if (++comp == OF_IR_VEC_SIZE)
			break;

		lsnprintf(&buf, &maxlen, ", ");
	} while (1);

	lsnprintf(&buf, &maxlen, "%s", reg->flags & OF_IR_REG_ABS ? "|" : "]");
}

static void
format_dst_reg(struct of_ir_shader *shader, char *buf, size_t maxlen,
	       struct of_ir_register *reg)
{
	const struct of_ir_reg_info *info;
	unsigned comp;

	info = of_ir_get_reg_info(shader, reg->type);

	lsnprintf(&buf, &maxlen, "[");

	comp = 0;
	do {
		if (!(reg->mask & (1 << comp)))
			lsnprintf(&buf, &maxlen, "_______");
		else if (reg->type == OF_IR_REG_VAR
			   || reg->type == OF_IR_REG_VARC)
			lsnprintf(&buf, &maxlen, "%2s%03d  ",
					info->name, reg->var[comp]);
		else
			lsnprintf(&buf, &maxlen, "%2s%03d.%c",
					info->name, reg->num,
					"xyzw"[reg->swizzle[comp]]);

		if (++comp == OF_IR_VEC_SIZE)
			break;

		lsnprintf(&buf, &maxlen, ", ");
	} while (1);

	lsnprintf(&buf, &maxlen, "]");
}

static void
format_target(struct of_ir_shader *shader, char *buf, size_t maxlen,
	      struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *parent = node->parent;

	assert(parent);

	while (node->parent_list.next == &parent->nodes) {
		node = node->parent;
		if (!node) {
			snprintf(buf, maxlen, "[invalid target]");
			return;
		}
	}

	node = LIST_ENTRY(struct of_ir_ast_node, node->parent_list.next,
				parent_list);

	snprintf(buf, maxlen, "%p", node);
}

static void
dump_instruction(struct of_ir_shader *shader, struct of_ir_instruction *ins,
		 unsigned level)
{
	const struct of_ir_opc_info *opc_info;
	struct of_ir_register *dst;
	char tmp[64] = "";
	char op[16];
	unsigned reg;

	opc_info = of_ir_get_opc_info(ins->opc);

	dst = ins->dst;
	if (dst)
		format_dst_reg(shader, tmp, sizeof(tmp), dst);
	else if (ins->target)
		format_target(shader, tmp, sizeof(tmp), ins->target);

	snprintf(op, sizeof(op), "%s%s", opc_info->name,
			(dst && dst->flags & OF_IR_REG_SAT) ? "_sat" : "");

	_debug_printf("%*s%-11s %36s%s", level, "",
			op, tmp, tmp[0] ? ", " : "  ");

	for (reg = 0; reg < ins->num_srcs; ++reg) {
		format_src_reg(shader, tmp, sizeof(tmp), ins->srcs[reg]);
		_debug_printf("%s%s", reg ? ", " : "", tmp);
	}

	_debug_printf("\n");
}

static void
dump_list(struct of_ir_shader *shader, struct of_ir_ast_node *node,
	  unsigned level)
{
	struct of_ir_instruction *ins;

	if (LIST_IS_EMPTY(&node->list.instrs)) {
		_debug_printf("%*sNothing\n", level, "");
		return;
	}

	LIST_FOR_EACH_ENTRY(ins, &node->list.instrs, list)
		dump_instruction(shader, ins, level);
}

static void
dump_node(struct of_ir_shader *shader, struct of_ir_ast_node *node,
	  unsigned level, dump_ast_callback_t extra, void *extra_data)
{
	struct of_ir_ast_node *child;

	switch (node->type) {
	case OF_IR_NODE_REGION:
		_debug_printf("%*s%p: region {\n", level, "", node);
		break;
	case OF_IR_NODE_DEPART:
		if (LIST_IS_EMPTY(&node->nodes))
			_debug_printf("%*s%p: depart %p\n",
					level, "", node,
					node->depart_repeat.region);
		else
			_debug_printf("%*s%p: depart %p after {\n",
					level, "", node,
					node->depart_repeat.region);
		break;
	case OF_IR_NODE_IF_THEN: {
		char condition[16];

		format_src_reg(shader, condition, sizeof(condition),
				node->if_then.reg);
		_debug_printf("%*s%p: if %s then {\n", level, "", node,
				condition);
		break; }
	case OF_IR_NODE_REPEAT:
		if (LIST_IS_EMPTY(&node->nodes))
			_debug_printf("%*s%p: repeat %p\n",
					level, "", node,
					node->depart_repeat.region);
		else
			_debug_printf("%*s%p: repeat %p after {\n",
					level, "", node,
					node->depart_repeat.region);
		break;
	case OF_IR_NODE_LIST:
		_debug_printf("%*s%p: list {\n", level, "", node);
		dump_list(shader, node, level + 4);
		_debug_printf("%*s}\n", level, "");
		return;
	default:
		assert(0);
	}

	if (extra)
		extra(shader, node, level, false, extra_data);

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list)
		dump_node(shader, child, level + 4, extra, extra_data);

	if (extra)
		extra(shader, node, level, true, extra_data);

	if (!LIST_IS_EMPTY(&node->nodes))
		_debug_printf("%*s}\n", level, "");
}

void
of_ir_dump_ast(struct of_ir_shader *shader, dump_ast_callback_t extra,
	       void *extra_data)
{
	struct of_ir_ast_node *node;

	LIST_FOR_EACH_ENTRY(node, &shader->root_nodes, parent_list)
		dump_node(shader, node, 0, extra, extra_data);
}

int
of_ir_shader_assemble(struct of_context *ctx, struct of_ir_shader *shader,
		      struct of_shader_stateobj *so)
{
	int ret;

	DBG("AST (pre-clean)");
	of_ir_dump_ast(shader, NULL, NULL);

	clean_ast(shader);

	DBG("AST (post-clean/pre-ssa)");
	of_ir_dump_ast(shader, NULL, NULL);

	ret = of_ir_to_ssa(shader);
	if (ret) {
		ERROR_MSG("failed to create SSA form");
		return -1;
	}

	ret = of_ir_optimize(shader);
	if (ret) {
		ERROR_MSG("failed to optimize shader");
		return -1;
	}

	ret = of_ir_assign_registers(shader);
	if (ret) {
		ERROR_MSG("failed to create executable form");
		return -1;
	}

	ret = of_ir_generate_code(ctx, shader);
	if (ret) {
		ERROR_MSG("failed to generate code");
		return -1;
	}

	pipe_resource_reference(&so->buffer, NULL);
	so->buffer = shader->buffer;
	so->num_instrs = shader->stats.num_instrs;

	return 0;
}
