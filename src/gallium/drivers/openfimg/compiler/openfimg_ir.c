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
	}

const struct of_ir_opc_info of_ir_opc_info[] = {
	OF_IR_OPC(NOP, ALU, 0),
	OF_IR_OPC(MOV, ALU, 1),
	OF_IR_OPC(MOVA, ALU, 1),
	OF_IR_OPC(MOVC, ALU, 2),
	OF_IR_OPC(ADD, ALU, 2),
	OF_IR_OPC(MUL, ALU, 2),
	OF_IR_OPC(MUL_LIT, ALU, 2),
	OF_IR_OPC(DP3, ALU, 2),
	OF_IR_OPC(DP4, ALU, 2),
	OF_IR_OPC(DPH, ALU, 2),
	OF_IR_OPC(DST, ALU, 2),
	OF_IR_OPC(EXP, ALU, 1),
	OF_IR_OPC(EXP_LIT, ALU, 1),
	OF_IR_OPC(LOG, ALU, 1),
	OF_IR_OPC(LOG_LIT, ALU, 1),
	OF_IR_OPC(RCP, ALU, 1),
	OF_IR_OPC(RSQ, ALU, 1),
	OF_IR_OPC(DP2ADD, ALU, 3),
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
	OF_IR_OPC(TEXLD, ALU, 2),
	OF_IR_OPC(CUBEDIR, ALU, 1),
	OF_IR_OPC(MAXCOMP, ALU, 1),
	OF_IR_OPC(TEXLDC, ALU, 3),
	OF_IR_OPC(TEXKILL, ALU, 1),
	OF_IR_OPC(MOVIPS, ALU, 1),
	OF_IR_OPC(ADDI, ALU, 2),
	OF_IR_OPC(B, CF, 0),
	OF_IR_OPC(BF, CF, 1),
	OF_IR_OPC(BP, CF, 0),
	OF_IR_OPC(BFP, CF, 1),
	OF_IR_OPC(BZP, CF, 1),
	OF_IR_OPC(CALL, SUB, 0),
	OF_IR_OPC(CALLNZ, SUB, 1),
	OF_IR_OPC(RET, ALU, 0), /* Not really ALU, but emitted the same way. */
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

	DEBUG_MSG("%x, %d, %s", flags, num, swizzle);
	reg->flags = flags;
	reg->type = type;
	reg->num = num;
	memcpy(reg->swizzle, swizzle, sizeof(reg->swizzle));

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
	assert(!instr->dst);
	instr->dst = reg;
}

void
of_ir_instr_add_src(struct of_ir_instruction *instr, struct of_ir_register *reg)
{
	assert(instr->num_srcs < ARRAY_SIZE(instr->srcs));
	instr->srcs[instr->num_srcs++] = reg;
}

void
of_ir_instr_insert(struct of_ir_shader *shader, struct of_ir_ast_node *node,
		   struct of_ir_instruction *where,
		   struct of_ir_instruction *instr)
{
	const struct of_ir_opc_info *info = of_ir_get_opc_info(instr->opc);
	struct list_head *list;

	assert(instr->num_srcs == info->num_srcs);
	assert(info->type != OF_IR_CF);
	assert(node || where);

	if (instr->dst && instr->dst->type == OF_IR_REG_R)
		shader->stats.num_vars = max(shader->stats.num_vars,
						instr->dst->num + 1);

	++shader->num_instrs;

	if (where) {
		/* Explicitly specified instruction to prepend. */
		node = where->node;
		list = &where->list;
	} else {
		/* Explicitly specified list node. */
		assert(node->type == OF_IR_NODE_LIST);
		list = &node->list.instrs;
	}

	list_addtail(&instr->list, list);
	instr->node = node;
	++node->list.num_instrs;
}

static void
merge_mask(struct of_ir_register *reg, const char *mask)
{
	unsigned comp;

	for (comp = 0; comp < ARRAY_SIZE(reg->swizzle); ++comp)
		if (mask[comp] != "xyzw"[comp])
			reg->swizzle[comp] = '_';
}

static void
merge_swizzle(struct of_ir_register *reg, const char *swizzle)
{
	unsigned comp;
	char result[4];

	for (comp = 0; comp < ARRAY_SIZE(reg->swizzle); ++comp) {
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

void
of_ir_node_insert(struct of_ir_ast_node *where, struct of_ir_ast_node *node)
{
	list_del(&node->parent_list);
	list_addtail(&node->parent_list, &where->nodes);
	node->parent = where;
	++where->num_nodes;
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

/*
 * Shader-level operations.
 */

struct of_ir_shader *
of_ir_shader_create(enum of_ir_shader_type type)
{
	struct of_ir_shader *shader;

	DEBUG_MSG("");
	shader = CALLOC_STRUCT(of_ir_shader);
	if (!shader)
		return NULL;

	shader->heap = of_heap_create();

	LIST_INITHEAD(&shader->root_nodes);

	if (type == OF_IR_SHADER_VERTEX)
		shader->reg_info = vs_reg_info;
	else if (type == OF_IR_SHADER_PIXEL)
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
format_src_reg(struct of_ir_shader *shader, char *buf, size_t maxlen,
	       struct of_ir_register *reg)
{
	const struct of_ir_reg_info *info;
	char ver_str[16] = "";

	info = of_ir_get_reg_info(shader, reg->type);

	if (reg->ver)
		snprintf(ver_str, sizeof(ver_str), ".%d", reg->ver);

	snprintf(buf, maxlen, "%s%s%s%d%s.%.4s%s",
			reg->flags & OF_IR_REG_NEGATE ? "-" : "",
			reg->flags & OF_IR_REG_ABS ? "|" : "",
			info->name, reg->num, ver_str, reg->swizzle,
			reg->flags & OF_IR_REG_ABS ? "|" : "");
}

static void
format_dst_reg(struct of_ir_shader *shader, char *buf, size_t maxlen,
	       struct of_ir_register *reg)
{
	const struct of_ir_reg_info *info;
	char ver_str[16] = "";

	info = of_ir_get_reg_info(shader, reg->type);

	if (reg->ver)
		snprintf(ver_str, sizeof(ver_str), ".%d", reg->ver);

	snprintf(buf, maxlen, "%s%d%s.%.4s", info->name, reg->num,
			ver_str, reg->swizzle);
}

static void
dump_instruction(struct of_ir_shader *shader, struct of_ir_instruction *ins,
		 unsigned level)
{
	const struct of_ir_opc_info *opc_info;
	struct of_ir_register *dst, *src[3];
	char dst_str[32], src_str[3][32];
	unsigned reg;

	opc_info = of_ir_get_opc_info(ins->opc);

	dst = ins->dst;
	if (dst)
		format_dst_reg(shader, dst_str, sizeof(dst_str), ins->dst);
	for (reg = 0; reg < 3; ++reg) {
		src[reg] = ins->srcs[reg];
		if (src[reg])
			format_src_reg(shader, src_str[reg],
					sizeof(src_str[reg]), src[reg]);
	}

	_debug_printf("%*s%s%s " "%s%s" "%s" "%s%s" "%s%s\n",
			level, "", opc_info->name,
			(dst && dst->flags & OF_IR_REG_SAT) ? "_sat" : "",
			dst ? dst_str : "", dst ? ", " : "",
			src[0] ? src_str[0] : "",
			src[1] ? ", " : "", src[1] ? src_str[1] : "",
			src[2] ? ", " : "", src[2] ? src_str[2] : "");
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
		_debug_printf("%*sregion %p {\n", level, "", node);
		break;
	case OF_IR_NODE_DEPART:
		if (LIST_IS_EMPTY(&node->nodes))
			_debug_printf("%*sdepart %p\n",
					level, "", node->depart_repeat.region);
		else
			_debug_printf("%*sdepart %p after {\n",
					level, "", node->depart_repeat.region);
		break;
	case OF_IR_NODE_IF_THEN: {
		char condition[16];

		format_src_reg(shader, condition, sizeof(condition),
				node->if_then.reg);
		_debug_printf("%*sif %s then {\n", level, "", condition);
		break; }
	case OF_IR_NODE_REPEAT:
		if (LIST_IS_EMPTY(&node->nodes))
			_debug_printf("%*srepeat %p\n",
					level, "", node->depart_repeat.region);
		else
			_debug_printf("%*srepeat %p after {\n",
					level, "", node->depart_repeat.region);
		break;
	case OF_IR_NODE_LIST:
		dump_list(shader, node, level);
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

/*
 * Assembler.
 */
#if 0
static uint32_t
dst_mask(struct of_ir_register *reg)
{
	uint32_t swiz = 0x0;
	int i;

	DEBUG_MSG("alu dst R%d.%s", reg->num, reg->swizzle);

	if (!reg->swizzle)
		return 0xf;
	
	for (i = 3; i >= 0; i--) {
		swiz <<= 1;
		if (reg->swizzle[i] == "xyzw"[i]) {
			swiz |= 0x1;
		} else if (reg->swizzle[i] != '_') {
			ERROR_MSG("invalid dst swizzle: %s", reg->swizzle);
			break;
		}
	}

	return swiz;
}

static uint32_t
src_swiz(struct of_ir_register *reg)
{
	uint32_t swiz = 0x00;
	int i;

	DEBUG_MSG("vector src R%d.%s", reg->num, reg->swizzle);

	if (!reg->swizzle)
		return 0xe4;

	for (i = 0; i < 4; ++i) {
		swiz >>= 2;
		switch (reg->swizzle[i]) {
		case 'x': swiz |= 0x0 << 6; break;
		case 'y': swiz |= 0x1 << 6; break;
		case 'z': swiz |= 0x2 << 6; break;
		case 'w': swiz |= 0x3 << 6; break;
		default:
			ERROR_MSG("invalid vector src swizzle: %s",
					reg->swizzle);
		}
	}

	return swiz;
}

static void
instr_emit(struct of_ir_shader *shader, struct of_ir_instruction *instr,
	   uint32_t *buffer, unsigned pc)
{
	uint32_t *dwords = buffer + 4 * pc;
	const struct of_ir_reg_info *reg;
	struct of_ir_register *src0_reg;
	struct of_ir_register *src1_reg;
	struct of_ir_register *src2_reg;
	struct of_ir_register *dst_reg;

	memset(dwords, 0, 4 * sizeof(uint32_t));

	dwords[2] |= INSTR_WORD2_OPCODE(instr->opc);

	if (instr->flags & OF_IR_INSTR_NEXT_3SRC)
		dwords[2] |= INSTR_WORD2_NEXT_3SRC;

	/* Source register 1 */
	src2_reg = instr->srcs[2];
	if (src2_reg) {
		reg = &shader->reg_info[src2_reg->type];

		dwords[0] |= ALU_WORD0_SRC2_NUM(src2_reg->num) |
				ALU_WORD0_SRC2_TYPE(reg->src_type) |
				ALU_WORD0_SRC2_SWIZZLE(src_swiz(src2_reg));

		if (src2_reg->flags & OF_IR_REG_NEGATE)
			dwords[0] |= ALU_WORD0_SRC2_NEGATE;

		if (src2_reg->flags & OF_IR_REG_ABS)
			dwords[0] |= ALU_WORD0_SRC2_ABS;
	}

	/* Source register 1 */
	src1_reg = instr->srcs[1];
	if (src1_reg) {
		reg = &shader->reg_info[src1_reg->type];

		dwords[0] |= ALU_WORD0_SRC1_NUM(src1_reg->num);
		dwords[1] |= ALU_WORD1_SRC1_TYPE(reg->src_type) |
				ALU_WORD1_SRC1_SWIZZLE(src_swiz(src1_reg));

		if (src1_reg->flags & OF_IR_REG_NEGATE)
			dwords[1] |= ALU_WORD1_SRC1_NEGATE;

		if (src1_reg->flags & OF_IR_REG_ABS)
			dwords[1] |= ALU_WORD1_SRC1_ABS;
	}

	/* Source register 0 (always used) */
	src0_reg = instr->srcs[0];
	if (src0_reg) {
		reg = &shader->reg_info[src0_reg->type];

		dwords[1] |= INSTR_WORD1_SRC0_NUM(src0_reg->num) |
				INSTR_WORD1_SRC0_TYPE(reg->src_type);

		if (src0_reg->flags & OF_IR_REG_NEGATE)
			dwords[1] |= INSTR_WORD1_SRC0_NEGATE;

		if (src0_reg->flags & OF_IR_REG_ABS)
			dwords[1] |= INSTR_WORD1_SRC0_ABS;

		dwords[2] |= INSTR_WORD2_SRC0_SWIZZLE(src_swiz(src0_reg));
	}

	/* Destination register */
	dst_reg = instr->dst;
	if (dst_reg) {
		reg = &shader->reg_info[dst_reg->type];

		dwords[2] |= ALU_WORD2_DST_NUM(dst_reg->num) |
				ALU_WORD2_DST_TYPE(reg->dst_type) |
				ALU_WORD2_DST_MASK(dst_mask(dst_reg));

		if (dst_reg->flags & OF_IR_REG_SAT)
			dwords[2] |= ALU_WORD2_DST_SAT;
	}

	if (instr->target) {
		int offset = instr->target->address - pc;
		bool backwards = offset < 0;

		if (backwards) {
			dwords[2] |= CF_WORD2_JUMP_BACK;
			offset = -offset;
		}

		dwords[2] |= CF_WORD2_JUMP_OFFS(offset);
	}

	/* TODO: Implement predicate support */
}
#endif
static int
of_ir_optimize(struct of_ir_shader *shader)
{
	/* TODO */
	return 0;
}

static int
of_ir_assign_registers(struct of_ir_shader *shader)
{
	/* TODO */
	return 0;
}

static int
of_ir_insert_cf(struct of_ir_shader *shader)
{
	/* TODO */
	return 0;
}

int
of_ir_shader_assemble(struct of_context *ctx, struct of_ir_shader *shader,
		      struct of_shader_stateobj *so)
{
	uint32_t *dwords = NULL;
	struct pipe_resource *buffer;
	struct pipe_transfer *transfer;
	int ret;

	if (!shader->num_instrs) {
		DBG("got empty shader");
		pipe_resource_reference(&so->buffer, NULL);
		so->num_instrs = 0;
		return 0;
	}

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

	ret = of_ir_insert_cf(shader);
	if (ret) {
		ERROR_MSG("failed to insert CF instructions");
		return -1;
	}

	buffer = pipe_buffer_create(ctx->base.screen,
					PIPE_BIND_CUSTOM, PIPE_USAGE_IMMUTABLE,
					16 * shader->num_instrs);
	if (!buffer) {
		ERROR_MSG("shader BO allocation failed");
		return -1;
	}

	dwords = pipe_buffer_map(&ctx->base, buffer, PIPE_TRANSFER_WRITE,
					&transfer);
	if (!dwords) {
		ERROR_MSG("failed to map shader BO");
		goto fail;
	}

	/*
	 * TODO: Emit instructions.
	 */

	if (transfer)
		pipe_buffer_unmap(&ctx->base, transfer);

	pipe_resource_reference(&so->buffer, NULL);
	so->buffer = buffer;
	so->num_instrs = shader->num_instrs;

	return 0;

fail:
	if (transfer)
		pipe_buffer_unmap(&ctx->base, transfer);
	if (buffer)
		pipe_resource_reference(&buffer, NULL);
	return -1;
}
