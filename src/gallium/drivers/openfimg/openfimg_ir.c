/*
 * Copyright (C) 2013 Tomasz Figa <tomasz.figa@gmail.com>
 *
 * Parts shamelessly copied from Freedreno driver:
 *
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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

#include "openfimg_ir.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "openfimg_util.h"
#include "openfimg_instr.h"

#define DEBUG_MSG(f, ...)  do { if (0) DBG(f, ##__VA_ARGS__); } while (0)
#define WARN_MSG(f, ...)   DBG("WARN:  "f, ##__VA_ARGS__)
#define ERROR_MSG(f, ...)  DBG("ERROR: "f, ##__VA_ARGS__)

#define REG_MASK 0x3f

/* simple allocator to carve allocations out of an up-front allocated heap,
 * so that we can free everything easily in one shot.
 */
static void * ir2_alloc(struct ir2_shader *shader, int sz)
{
	void *ptr = &shader->heap[shader->heap_idx];
	shader->heap_idx += align(sz, 4);
	return ptr;
}

static char * ir2_strdup(struct ir2_shader *shader, const char *str)
{
	char *ptr = NULL;
	if (str) {
		int len = strlen(str);
		ptr = ir2_alloc(shader, len+1);
		memcpy(ptr, str, len);
		ptr[len] = '\0';
	}
	return ptr;
}

struct ir2_shader * ir2_shader_create(void)
{
	DEBUG_MSG("");
	return calloc(1, sizeof(struct ir2_shader));
}

void ir2_shader_destroy(struct ir2_shader *shader)
{
	DEBUG_MSG("");
	free(shader);
}

struct ir2_instruction * ir2_instr_create(struct ir2_shader *shader,
					  instr_opc_t opc)
{
	struct ir2_instruction *instr =
			ir2_alloc(shader, sizeof(struct ir2_instruction));
	DEBUG_MSG("%d", opc);
	instr->shader = shader;
	instr->pred = shader->pred;
	instr->opc = opc;
	assert(shader->instrs_count < ARRAY_SIZE(shader->instrs));
	shader->instrs[shader->instrs_count++] = instr;
	return instr;
}

static void reg_update_stats(struct ir2_register *reg,
		struct ir2_shader_info *info, bool dest)
{
	if (dest) {
		switch (reg->type) {
		case REG_DST_R:
			info->max_reg = MAX2(info->max_reg, reg->num);
			info->regs_written |= (1 << reg->num);
			break;
		default:
			break;
		}
	} else {
		switch (reg->type) {
		case REG_SRC_R:
			info->max_reg = MAX2(info->max_reg, reg->num);
			break;
		case REG_SRC_V:
			info->max_input_reg = MAX2(info->max_input_reg,
								reg->num);
			break;
		default:
			break;
		}
	}
}

/* actually, a write-mask */
static uint32_t reg_alu_dst_swiz(struct ir2_register *reg)
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

static uint32_t reg_alu_src_swiz(struct ir2_register *reg)
{
	uint32_t swiz = 0x00;
	int i;

	DEBUG_MSG("vector src R%d.%s", reg->num, reg->swizzle);

	if (!reg->swizzle)
		return 0xe4;

	for (i = 3; i >= 0; i--) {
		swiz <<= 2;
		switch (reg->swizzle[i]) {
		case 'x': swiz |= 0x0; break;
		case 'y': swiz |= 0x1; break;
		case 'z': swiz |= 0x2; break;
		case 'w': swiz |= 0x3; break;
		default:
			ERROR_MSG("invalid vector src swizzle: %s",
					reg->swizzle);
		}
	}

	return swiz;
}

/*
 * Control flow instructions:
 */

static int instr_emit_cf(struct ir2_instruction *instr, uint32_t *dwords,
			 struct ir2_shader_info *info)
{
#warning TODO
	return 0;
}

/*
 * ALU instructions:
 */

static int instr_emit_alu(struct ir2_instruction *instr, uint32_t *dwords,
			  struct ir2_shader_info *info)
{
	int reg = 0;
	instr_t *alu = (instr_t *)dwords;
	struct ir2_register *dst_reg  = instr->regs[reg++];
	struct ir2_register *src0_reg;
	struct ir2_register *src1_reg;
	struct ir2_register *src2_reg = NULL;

	memset(alu, 0, sizeof(*alu));

	/*
	 * handle instructions w/ 3 src operands
	 *
	 * note: disassembler lists 3rd src first, ie:
	 *   MULADDv Rdst = Rsrc2 + (Rsrc0 * Rsrc1)
	 * which is the reason for this strange ordering.
	 */
	if (instr->regs_count >= 4)
		src2_reg = instr->regs[reg++];

	src0_reg = instr->regs[reg++];
	src1_reg = instr->regs[reg++];

	reg_update_stats(dst_reg, info, true);
	reg_update_stats(src0_reg, info, false);
	reg_update_stats(src1_reg, info, false);

	assert(dst_reg->flags == 0);
	assert(!dst_reg->swizzle || (strlen(dst_reg->swizzle) == 4));
	assert(!src0_reg->swizzle || (strlen(src0_reg->swizzle) == 4));
	assert(!src1_reg->swizzle || (strlen(src1_reg->swizzle) == 4));

	// TODO predicate case/condition.. need to add to parser

	if (src2_reg) {
		reg_update_stats(src2_reg, info, false);

		assert(!src2_reg->swizzle || (strlen(src2_reg->swizzle) == 4));

		alu->src2_regnum	= src2_reg->num;
		alu->src2_regtype	= src2_reg->type;
		alu->src2_negate	= !!(src2_reg->flags & IR2_REG_NEGATE);
		alu->src2_abs		= !!(src2_reg->flags & IR2_REG_ABS);
		alu->src2_swizzle	= reg_alu_src_swiz(src2_reg);
	}

	alu->src1_regnum	= src1_reg->num;
	alu->src1_regtype	= src1_reg->type;
	alu->src1_negate	= !!(src1_reg->flags & IR2_REG_NEGATE);
	alu->src1_abs		= !!(src1_reg->flags & IR2_REG_ABS);
	alu->src1_swizzle	= reg_alu_src_swiz(src1_reg);

	alu->src0_regnum	= src0_reg->num;
	alu->src0_regtype	= src0_reg->type;
	alu->src0_negate	= !!(src0_reg->flags & IR2_REG_NEGATE);
	alu->src0_abs		= !!(src0_reg->flags & IR2_REG_ABS);
	alu->src0_swizzle	= reg_alu_src_swiz(src0_reg);

	alu->dest_regnum	= dst_reg->num;
	alu->dest_regtype	= dst_reg->type;
	alu->dest_clamp		= instr->clamp;
	alu->dest_mask		= reg_alu_dst_swiz(dst_reg);

	// TODO use multiple predicate channels
	alu->pred_channel	= 0;
	alu->pred_unknown	= 0;
	alu->pred_enable	= (instr->pred != IR2_PRED_NONE);
	alu->pred_negate	= (instr->pred == IR2_PRED_NE);
	alu->opcode		= instr->opc;

	return 0;
}

static int instr_emit(struct ir2_instruction *instr, uint32_t *dwords,
		uint32_t idx, struct ir2_shader_info *info)
{
	switch (instr->instr_type) {
	case IR2_CF:
		return instr_emit_cf(instr, dwords, info);
	case IR2_ALU:
		return instr_emit_alu(instr, dwords, info);
	}
	return -1;
}

void * ir2_shader_assemble(struct ir2_shader *shader, struct ir2_shader_info *info)
{
	uint32_t i;
	uint32_t *ptr, *dwords = NULL;
	uint32_t idx = 0;
	int ret;

	info->sizedwords    = 4 * shader->instrs_count;
	info->max_reg       = -1;
	info->max_input_reg = 0;
	info->regs_written  = 0;

	ptr = dwords = calloc(1, 4 * info->sizedwords);

	/* third pass, emit ALU/FETCH: */
	for (i = 0; i < shader->instrs_count; i++) {
		ret = instr_emit(shader->instrs[i], ptr, idx++, info);
		if (ret) {
			ERROR_MSG("instruction emit failed: %d", ret);
			goto fail;
		}
		ptr += 4;
		assert((ptr - dwords) <= info->sizedwords);
	}

	return dwords;

fail:
	free(dwords);
	return NULL;
}

struct ir2_register * ir2_reg_create(struct ir2_instruction *instr,
		int num, const char *swizzle, int flags)
{
	struct ir2_register *reg =
			ir2_alloc(instr->shader, sizeof(struct ir2_register));
	DEBUG_MSG("%x, %d, %s", flags, num, swizzle);
	assert(num <= REG_MASK);
	reg->flags = flags;
	reg->num = num;
	reg->swizzle = ir2_strdup(instr->shader, swizzle);
	assert(instr->regs_count < ARRAY_SIZE(instr->regs));
	instr->regs[instr->regs_count++] = reg;
	return reg;
}
