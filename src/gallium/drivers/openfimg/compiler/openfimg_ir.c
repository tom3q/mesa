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

const struct of_ir_opc_info of_ir_opc_info[] = {
	[OF_OP_NOP] = {
		.type = OF_IR_ALU,
		.num_srcs = 0,
	},
	[OF_OP_MOV] = {
		.type = OF_IR_ALU,
		.num_srcs = 1,
	},
	[OF_OP_MOVA] = {
		.type = OF_IR_ALU,
		.num_srcs = 1,
	},
	[OF_OP_MOVC] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_ADD] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_MUL] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_MUL_LIT] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_DP3] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_DP4] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_DPH] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_DST] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_EXP] = {
		.type = OF_IR_ALU,
		.num_srcs = 1,
	},
	[OF_OP_EXP_LIT] = {
		.type = OF_IR_ALU,
		.num_srcs = 1,
	},
	[OF_OP_LOG] = {
		.type = OF_IR_ALU,
		.num_srcs = 1,
	},
	[OF_OP_LOG_LIT] = {
		.type = OF_IR_ALU,
		.num_srcs = 1,
	},
	[OF_OP_RCP] = {
		.type = OF_IR_ALU,
		.num_srcs = 1,
	},
	[OF_OP_RSQ] = {
		.type = OF_IR_ALU,
		.num_srcs = 1,
	},
	[OF_OP_DP2ADD] = {
		.type = OF_IR_ALU,
		.num_srcs = 3,
	},
	[OF_OP_MAX] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_MIN] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_SGE] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_SLT] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_SETP_EQ] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_SETP_GE] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_SETP_GT] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_SETP_NE] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_CMP] = {
		.type = OF_IR_ALU,
		.num_srcs = 3,
	},
	[OF_OP_MAD] = {
		.type = OF_IR_ALU,
		.num_srcs = 3,
	},
	[OF_OP_FRC] = {
		.type = OF_IR_ALU,
		.num_srcs = 1,
	},
	[OF_OP_FLR] = {
		.type = OF_IR_ALU,
		.num_srcs = 1,
	},
	[OF_OP_TEXLD] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_CUBEDIR] = {
		.type = OF_IR_ALU,
		.num_srcs = 1,
	},
	[OF_OP_MAXCOMP] = {
		.type = OF_IR_ALU,
		.num_srcs = 1,
	},
	[OF_OP_TEXLDC] = {
		.type = OF_IR_ALU,
		.num_srcs = 3,
	},
	[OF_OP_TEXKILL] = {
		.type = OF_IR_ALU,
		.num_srcs = 1,
	},
	[OF_OP_MOVIPS] = {
		.type = OF_IR_ALU,
		.num_srcs = 1,
	},
	[OF_OP_ADDI] = {
		.type = OF_IR_ALU,
		.num_srcs = 2,
	},
	[OF_OP_B] = {
		.type = OF_IR_CF,
		.num_srcs = 0,
	},
	[OF_OP_BF] = {
		.type = OF_IR_CF,
		.num_srcs = 1,
	},
	[OF_OP_BP] = {
		.type = OF_IR_CF,
		.num_srcs = 0,
	},
	[OF_OP_BFP] = {
		.type = OF_IR_CF,
		.num_srcs = 1,
	},
	[OF_OP_BZP] = {
		.type = OF_IR_CF,
		.num_srcs = 1,
	},
	[OF_OP_CALL] = {
		.type = OF_IR_CF,
		.num_srcs = 0,
	},
	[OF_OP_CALLNZ] = {
		.type = OF_IR_CF,
		.num_srcs = 1,
	},
	[OF_OP_RET] = {
		.type = OF_IR_CF,
		.num_srcs = 0,
	},
};

static const struct of_ir_reg_info vs_reg_info[OF_IR_NUM_REG_TYPES] = {
	[OF_IR_REG_R] = {
		.src_type = OF_SRC_R,
		.dst_type = OF_DST_R,
		.num_reads = 2,
		.num_regs = 32,
		.writable = true,
		.readable = true,
		.al_addr = true,
	},
	[OF_IR_REG_V] = {
		.src_type = OF_SRC_V,
		.num_reads = 1,
		.num_regs = 10,
		.readable = true,
		.al_addr = true,
	},
	[OF_IR_REG_C] = {
		.src_type = OF_SRC_C,
		.num_reads = 1,
		.num_regs = 256,
		.readable = true,
		.a0_addr = true,
		.al_addr = true,
	},
	[OF_IR_REG_I] = {
		.src_type = OF_SRC_I,
		.num_reads = 1,
		.num_regs = 16,
		.readable = true,
	},
	[OF_IR_REG_AL] = {
		.src_type = OF_SRC_AL,
		.dst_type = OF_DST_AL,
		.num_reads = 1,
		.num_regs = 4,
		.writable = true,
		.readable = true,
	},
	[OF_IR_REG_B] = {
		.src_type = OF_SRC_B,
		.num_reads = 1,
		.num_regs = 16,
		.readable = true,
		.scalar = true,
	},
	[OF_IR_REG_P] = {
		.src_type = OF_SRC_P,
		.dst_type = OF_DST_P,
		.num_reads = 1,
		.num_regs = 7,
		.writable = true,
		.readable = true,
	},
	[OF_IR_REG_S] = {
		.src_type = OF_SRC_S,
		.num_reads = 1,
		.num_regs = 4,
		.readable = true,
	},
	[OF_IR_REG_O] = {
		.dst_type = OF_DST_O,
		.num_regs = 10,
		.writable = true,
		.al_addr = true,
	},
	[OF_IR_REG_A0] = {
		.dst_type = OF_DST_A0,
		.num_regs = 1,
		.writable = true,
	},
};

static const struct of_ir_reg_info ps_reg_info[OF_IR_NUM_REG_TYPES] = {
	[OF_IR_REG_R] = {
		.src_type = OF_SRC_R,
		.dst_type = OF_DST_R,
		.num_reads = 2,
		.num_regs = 32,
		.writable = true,
		.readable = true,
		.al_addr = true,
	},
	[OF_IR_REG_V] = {
		.src_type = OF_SRC_V,
		.num_reads = 1,
		.num_regs = 8,
		.readable = true,
		.al_addr = true,
	},
	[OF_IR_REG_C] = {
		.src_type = OF_SRC_C,
		.num_reads = 1,
		.num_regs = 256,
		.readable = true,
		.a0_addr = true,
		.al_addr = true,
	},
	[OF_IR_REG_I] = {
		.src_type = OF_SRC_I,
		.num_reads = 1,
		.num_regs = 16,
		.readable = true,
	},
	[OF_IR_REG_AL] = {
		.src_type = OF_SRC_AL,
		.dst_type = OF_DST_AL,
		.num_reads = 1,
		.num_regs = 4,
		.writable = true,
		.readable = true,
	},
	[OF_IR_REG_B] = {
		.src_type = OF_SRC_B,
		.num_reads = 1,
		.num_regs = 16,
		.readable = true,
		.scalar = true,
	},
	[OF_IR_REG_P] = {
		.src_type = OF_SRC_P,
		.dst_type = OF_DST_P,
		.num_reads = 1,
		.num_regs = 7,
		.writable = true,
		.readable = true,
	},
	[OF_IR_REG_S] = {
		.src_type = OF_SRC_S,
		.num_reads = 1,
		.num_regs = 8,
		.readable = true,
	},
	[OF_IR_REG_D] = {
		.src_type = OF_SRC_D,
		.num_reads = 1,
		.num_regs = 8,
		.readable = true,
	},
	[OF_IR_REG_VFACE] = {
		.src_type = OF_SRC_VFACE,
		.num_reads = 1,
		.num_regs = 1,
		.readable = true,
		.scalar = true,
	},
	[OF_IR_REG_VPOS] = {
		.src_type = OF_SRC_VPOS,
		.num_reads = 1,
		.num_regs = 1,
		.readable = true,
		.scalar = true,
	},
	[OF_IR_REG_O] = {
		.dst_type = OF_DST_O,
		.num_regs = 1,
		.writable = true,
		.al_addr = true,
	},
	[OF_IR_REG_A0] = {
		.dst_type = OF_DST_A0,
		.num_regs = 1,
		.writable = true,
	},
};

/*
 * Utility functions.
 */

/*
 * Simple allocator to carve allocations out of an up-front allocated heap,
 * so that we can free everything easily in one shot.
 */
static void *
of_ir_alloc(struct of_ir_shader *shader, int sz)
{
	void *ptr = &shader->heap[shader->heap_idx];

	shader->heap_idx += align(sz, 4) / 4;

	return ptr;
}

/*
 * Register-level operations.
 */

struct of_ir_register *
of_ir_reg_create(struct of_ir_shader *shader, enum of_ir_reg_type type,
		 unsigned num, const char *swizzle, unsigned flags)
{
	struct of_ir_register *reg = of_ir_alloc(shader, sizeof(*reg));

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
	struct of_ir_register *reg = of_ir_alloc(shader, sizeof(*reg));

	memcpy(reg, src, sizeof(*reg));

	return reg;
}

struct of_ir_register *
of_ir_reg_temporary(struct of_ir_shader *shader)
{
	return of_ir_reg_create(shader, OF_IR_REG_R,
				32 + shader->num_temporaries++, "xyzw", 0);
}

struct of_ir_register *
of_ir_reg_predicate(struct of_ir_shader *shader)
{
	/* TODO */
	return NULL;
}

void
of_ir_reg_set_swizzle(struct of_ir_register *reg, const char *swizzle)
{
	memcpy(reg->swizzle, swizzle, sizeof(reg->swizzle));
}

/*
 * instruction-level operations.
 */

struct of_ir_instruction *
of_ir_instr_create(struct of_ir_shader *shader, enum of_instr_opcode opc)
{
	struct of_ir_instruction *instr = of_ir_alloc(shader, sizeof(*instr));

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

static void
of_ir_cf_branch(struct of_ir_shader *shader, struct of_ir_cf_block *cf,
		struct of_ir_cf_block *next_cf)
{
	unsigned target = cf->num_targets++;

	cf->targets[target].block = next_cf;
	list_addtail(&cf->targets[target].list, &next_cf->sources);
}

static void
of_ir_cf_split(struct of_ir_shader *shader, struct of_ir_instruction *instr)
{
	struct of_ir_cf_block *cf, *next_cf;
	struct list_head tmp_list;
	unsigned i;

	cf = instr->block;
	next_cf = of_ir_cf_create(shader);

	/* Move instructions after instr to next basic block. */
	list_inithead(&tmp_list);
	list_cut_position(&tmp_list, &cf->instrs, &instr->list);
	list_cut_position(&next_cf->instrs, &cf->instrs,
				list_tail(&cf->instrs));
	list_addtail(&next_cf->instrs, &tmp_list);
	list_del(&tmp_list);
	LIST_FOR_EACH_ENTRY(instr, &next_cf->instrs, list)
		instr->block = next_cf;

	/* Branch from next basic block to targets of original block. */
	for (i = 0; i < cf->num_targets; ++i) {
		of_ir_cf_branch(shader, next_cf, cf->targets[i].block);
		list_del(&cf->targets[i].list);
	}

	/* Make original block branch only to new block. */
	cf->num_targets = 0;
	of_ir_cf_branch(shader, cf, next_cf);

	/* Replace old block with new block on CF stack if present. */
	if (!LIST_IS_EMPTY(&cf->cf_stack_list)) {
		list_add(&next_cf->cf_stack_list, &cf->cf_stack_list);
		list_delinit(&cf->cf_stack_list);
	}
}

static void
of_ir_instr_cf_fixup(struct of_ir_shader *shader,
		     struct of_ir_instruction *instr)
{
	struct of_ir_cf_block *cf = instr->block;
	struct of_ir_instruction *dest = instr->target;
	struct of_ir_cf_block *dest_cf = NULL, *fall_cf;

	if (&instr->list != list_tail(&cf->instrs)) {
		/* Inserting CF in the middle of a block divides it. */
		of_ir_cf_split(shader, instr);
	} else if (cf->num_targets) {
		/*
		 * Appending CF instruction to a block that already has branch
		 * targets set divides it. The assumption is that CF is always
		 * the last instruction in the block.
		 */
		struct of_ir_instruction *prev;

		prev = LIST_ENTRY(struct of_ir_instruction,
					instr->list.prev, list);
		of_ir_cf_split(shader, prev);
	}

	if (dest) {
		/* Destination is specified explicitly. */
		if (&dest->list != list_head(&dest->block->instrs)) {
			/*
			 * If destination is not at start of a basic block
			 * then the block must be divided.
			 */
			struct of_ir_instruction *dest_prev;

			dest_prev = LIST_ENTRY(struct of_ir_instruction,
						dest->list.prev, list);
			of_ir_cf_split(shader, dest_prev);
		}
		dest_cf = dest->block;
	}

	if (!dest_cf)
		/* Create new basic block if destination not specified. */
		dest_cf = of_ir_cf_create(shader);

	switch (instr->opc) {
	case OF_OP_BFP:
	case OF_OP_BZP:
	case OF_OP_BF:
		/* Basic block for fall. */
		fall_cf = of_ir_cf_create(shader);
		of_ir_cf_branch(shader, cf, fall_cf);
		list_addtail(&fall_cf->cf_stack_list, &cf->cf_stack_list);
		/* Intentional fall through... */
	case OF_OP_BP:
	case OF_OP_B:
		/* Basic block for jump. */
		of_ir_cf_branch(shader, cf, dest_cf);
		list_addtail(&dest_cf->cf_stack_list, &cf->cf_stack_list);
		break;
	case OF_OP_CALL:
	case OF_OP_CALLNZ:
		/* TODO: Handle subroutine calls. */
	case OF_OP_RET:
		/* TODO: Handle subroutine returns. */
	default:
		assert(0);
	}

	/* Close current basic block. */
	list_delinit(&cf->cf_stack_list);
}

void
of_ir_instr_insert(struct of_ir_shader *shader, struct of_ir_cf_block *block,
		   struct of_ir_instruction *where,
		   struct of_ir_instruction *instr)
{
	const struct of_ir_opc_info *info = of_ir_get_opc_info(instr->opc);
	struct list_head *list;

	assert(instr->num_srcs == info->num_srcs);

	++shader->num_instrs;

	if (where) {
		/* Explicitly specified instruction to prepend. */
		block = where->block;
		list = &where->list;
	} else {
		/* Append to basic block. */
		if (!block) {
			/* Block not given - use currently active one or new. */
			if (LIST_IS_EMPTY(&shader->cf_stack))
				of_ir_cf_push(shader);
			block = LIST_ENTRY(struct of_ir_cf_block,
					list_head(&shader->cf_stack), list);
		}
		list = &block->instrs;
	}

	list_addtail(&instr->list, list);
	instr->block = block;

	/* A CF instruction ends the basic block. */
	if (info->type == OF_IR_CF)
		of_ir_instr_cf_fixup(shader, instr);
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
		case 'x': result[comp] = reg->swizzle[0];
		case 'y': result[comp] = reg->swizzle[1];
		case 'z': result[comp] = reg->swizzle[2];
		case 'w': result[comp] = reg->swizzle[3];
		default:
			ERROR_MSG("invalid vector src swizzle: %s",
					reg->swizzle);
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
			 struct of_ir_cf_block *block,
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

		of_ir_instr_insert(shader, block, where, instr);
		++instrs;
	}
}

/*
 * Basic block-level operations.
 */

struct of_ir_cf_block *
of_ir_cf_create(struct of_ir_shader *shader)
{
	struct of_ir_cf_block *cf = of_ir_alloc(shader, sizeof(*cf));
	int i;

	cf->shader = shader;

	LIST_INITHEAD(&cf->list);
	LIST_INITHEAD(&cf->psis);
	LIST_INITHEAD(&cf->instrs);

	for (i = 0; i < OF_IR_NUM_CF_TARGETS; ++i)
		LIST_INITHEAD(&cf->targets[i].list);

	return cf;
}

void
of_ir_cf_insert(struct of_ir_shader *shader, struct of_ir_cf_block *block)
{
	list_addtail(&block->list, &shader->cf_blocks);
	++shader->num_cf_blocks;
}

struct of_ir_cf_block *
of_ir_cf_push(struct of_ir_shader *shader)
{
	struct of_ir_cf_block *cf;

	cf = of_ir_cf_create(shader);
	of_ir_cf_insert(shader, cf);
	list_addtail(&cf->cf_stack_list, &shader->cf_stack);

	return cf;
}

void
of_ir_cf_pop(struct of_ir_shader *shader)
{
	struct list_head *item;

	assert(!LIST_IS_EMPTY(&shader->cf_stack));

	item = list_pop(&shader->cf_stack);
	list_inithead(item);
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

	LIST_INITHEAD(&shader->cf_blocks);
	LIST_INITHEAD(&shader->cf_stack);

	of_ir_cf_push(shader);

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
	DEBUG_MSG("");
	FREE(shader);
}

/*
 * Assembler.
 */

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
instr_emit_cf(struct of_ir_shader *shader, struct of_ir_cf_block *cf,
	      uint32_t *dwords)
{
	DBG("TODO");
}

static void
instr_emit_alu(struct of_ir_shader *shader, struct of_ir_instruction *instr,
	       uint32_t *dwords)
{
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

	/* TODO: Implement predicate support */
}

int
of_ir_shader_assemble(struct of_context *ctx, struct of_ir_shader *shader,
		      struct of_shader_stateobj *so)
{
	uint32_t *ptr, *dwords = NULL;
	struct pipe_resource *buffer;
	struct pipe_transfer *transfer;
	struct of_ir_cf_block *cf;
	unsigned idx = 0;

	assert(shader->num_instrs);

	buffer = pipe_buffer_create(ctx->base.screen,
					PIPE_BIND_CUSTOM, PIPE_USAGE_IMMUTABLE,
					16 * shader->num_instrs);
	if (!buffer) {
		ERROR_MSG("shader BO allocation failed");
		return -1;
	}

	ptr = dwords = pipe_buffer_map(&ctx->base, buffer, PIPE_TRANSFER_WRITE,
					&transfer);
	if (!ptr) {
		ERROR_MSG("failed to map shader BO");
		goto fail;
	}

	/*
	 * First pass:
	 * Emit ALU instructions and assign addresses to CF blocks.
	 */
	LIST_FOR_EACH_ENTRY(cf, &shader->cf_blocks, list) {
		struct of_ir_instruction *ins;

		/* Remember start address of the block. */
		cf->address = idx;

		/* Emit each instruction of the basic block. */
		LIST_FOR_EACH_ENTRY(ins, &cf->instrs, list) {
			instr_emit_alu(shader, ins, ptr);
			ptr += 4;
			++idx;
		}

		/*
		 * Keep one slot for CF instruction that will be emitted
		 * in second pass.
		 */
		ptr += 4;
		++idx;
	}

	/*
	 * Second pass:
	 * Emit CF instructions, as we know addresses of all basic blocks now.
	 */
	LIST_FOR_EACH_ENTRY(cf, &shader->cf_blocks, list)
		instr_emit_cf(shader, cf, dwords);

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
