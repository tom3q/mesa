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

#include "fimg_3dse.xml.h"

#define DEBUG_MSG(f, ...)  do { if (0) DBG(f, ##__VA_ARGS__); } while (0)
#define WARN_MSG(f, ...)   DBG("WARN:  "f, ##__VA_ARGS__)
#define ERROR_MSG(f, ...)  DBG("ERROR: "f, ##__VA_ARGS__)

#define REG_MASK 0x3f

/* simple allocator to carve allocations out of an up-front allocated heap,
 * so that we can free everything easily in one shot.
 */
static void *
of_ir_alloc(struct of_ir_shader *shader, int sz)
{
	void *ptr = &shader->heap[shader->heap_idx];
	shader->heap_idx += align(sz, 4);
	return ptr;
}

static char *
of_ir_strdup(struct of_ir_shader *shader, const char *str)
{
	char *ptr = NULL;
	if (str) {
		int len = strlen(str);
		ptr = of_ir_alloc(shader, len+1);
		memcpy(ptr, str, len);
		ptr[len] = '\0';
	}
	return ptr;
}

struct of_ir_shader *
of_ir_shader_create(void)
{
	DEBUG_MSG("");
	return calloc(1, sizeof(struct of_ir_shader));
}

void
of_ir_shader_destroy(struct of_ir_shader *shader)
{
	DEBUG_MSG("");
	free(shader);
}

static struct of_ir_instruction *
of_ir_instr_create(struct of_ir_shader *shader, enum of_instr_opcode opc,
		   enum of_ir_instr_type type)
{
	struct of_ir_instruction *instr =
			of_ir_alloc(shader, sizeof(struct of_ir_instruction));
	DEBUG_MSG("%d", opc);
	instr->opc = opc;
	instr->instr_type = type;
	instr->next_3arg = false;
	assert(shader->instrs_count < ARRAY_SIZE(shader->instrs));
	shader->instrs[shader->instrs_count++] = instr;
	return instr;
}

struct of_ir_instruction *
of_ir_instr_create_alu(struct of_ir_shader *shader, enum of_instr_opcode opc)
{
	return of_ir_instr_create(shader, opc, OF_IR_ALU);
}

struct of_ir_instruction *
of_ir_instr_create_cf(struct of_ir_shader *shader, enum of_instr_opcode opc)
{
	return of_ir_instr_create(shader, opc, OF_IR_CF);
}

static void
reg_update_stats(struct of_ir_register *reg, struct of_ir_shader_info *info,
		 bool dest)
{
	if (dest) {
		switch (reg->type) {
		case OF_DST_R:
			info->max_reg = MAX2(info->max_reg, reg->num);
			info->regs_written |= (1 << reg->num);
			break;
		default:
			break;
		}
	} else {
		switch (reg->type) {
		case OF_SRC_R:
			info->max_reg = MAX2(info->max_reg, reg->num);
			break;
		case OF_SRC_V:
			info->max_input_reg = MAX2(info->max_input_reg,
								reg->num);
			break;
		default:
			break;
		}
	}
}

/* actually, a write-mask */
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

/*
 * Control flow instructions:
 */

static void
instr_emit_cf(struct of_ir_instruction *instr, uint32_t *dwords,
	      struct of_ir_shader_info *info)
{
	DBG("TODO");
}

/*
 * ALU instructions:
 */

static void
instr_emit_alu(struct of_ir_instruction *instr, uint32_t *dwords,
	       struct of_ir_shader_info *info)
{
	int reg = 0;
	struct of_ir_register *dst_reg = instr->dst;
	struct of_ir_register *src0_reg;
	struct of_ir_register *src1_reg;
	struct of_ir_register *src2_reg;

	src0_reg = instr->src[reg++];
	src1_reg = instr->src[reg++];
	src2_reg = instr->src[reg++];

	reg_update_stats(dst_reg, info, true);
	reg_update_stats(src0_reg, info, false);

	/* Source register 1 */
	if (src2_reg) {
		reg_update_stats(src2_reg, info, false);

		assert(!src2_reg->swizzle || (strlen(src2_reg->swizzle) == 4));

		dwords[0] |= ALU_WORD0_SRC2_NUM(src2_reg->num) |
				ALU_WORD0_SRC2_TYPE(src2_reg->type) |
				ALU_WORD0_SRC2_SWIZZLE(src_swiz(src2_reg));

		if (src2_reg->flags & OF_IR_REG_NEGATE)
			dwords[0] |= ALU_WORD0_SRC2_NEGATE;

		if (src2_reg->flags & OF_IR_REG_ABS)
			dwords[0] |= ALU_WORD0_SRC2_ABS;
	}

	/* Source register 1 */
	if (src1_reg) {
		reg_update_stats(src1_reg, info, false);

		assert(!src1_reg->swizzle || (strlen(src1_reg->swizzle) == 4));

		dwords[0] |= ALU_WORD0_SRC1_NUM(src1_reg->num);
		dwords[1] |= ALU_WORD1_SRC1_TYPE(src1_reg->type) |
				ALU_WORD1_SRC1_SWIZZLE(src_swiz(src1_reg));

		if (src1_reg->flags & OF_IR_REG_NEGATE)
			dwords[1] |= ALU_WORD1_SRC1_NEGATE;

		if (src1_reg->flags & OF_IR_REG_ABS)
			dwords[1] |= ALU_WORD1_SRC1_ABS;
	}

	/* Source register 0 (always used) */
	assert(!src0_reg->swizzle || (strlen(src0_reg->swizzle) == 4));

	dwords[1] |= INSTR_WORD1_SRC0_NUM(src0_reg->num) |
			INSTR_WORD1_SRC0_TYPE(src0_reg->type);

	if (src0_reg->flags & OF_IR_REG_NEGATE)
		dwords[1] |= INSTR_WORD1_SRC0_NEGATE;

	if (src0_reg->flags & OF_IR_REG_ABS)
		dwords[1] |= INSTR_WORD1_SRC0_ABS;

	dwords[2] |= INSTR_WORD2_SRC0_SWIZZLE(src_swiz(src0_reg));

	/* Destination register */
	assert(!dst_reg->swizzle || (strlen(dst_reg->swizzle) == 4));

	dwords[2] |= ALU_WORD2_DST_NUM(dst_reg->num) |
			ALU_WORD2_DST_TYPE(dst_reg->type) |
			ALU_WORD2_DST_MASK(dst_mask(dst_reg));

	if (dst_reg->flags & OF_IR_REG_SAT)
		dwords[2] |= ALU_WORD2_DST_SAT;

	/* TODO: Implement prediate support */
}

static int
instr_emit(struct of_ir_instruction *instr, uint32_t *dwords, uint32_t idx,
	   struct of_ir_shader_info *info)
{
	memset(dwords, 0, 4 * sizeof(uint32_t));

	dwords[2] |= INSTR_WORD2_OPCODE(instr->opc);

	if (instr->next_3arg)
		dwords[2] |= INSTR_WORD2_NEXT_3SRC;

	switch (instr->instr_type) {
	case OF_IR_CF:
		instr_emit_cf(instr, dwords, info);
	case OF_IR_ALU:
		instr_emit_alu(instr, dwords, info);
	}

	return 0;
}

struct pipe_resource *
of_ir_shader_assemble(struct of_context *ctx, struct of_ir_shader *shader,
		      struct of_ir_shader_info *info)
{
	uint32_t i;
	uint32_t *ptr, *dwords = NULL;
	uint32_t idx = 0;
	int ret;
	struct pipe_resource *buffer;
	struct pipe_transfer *transfer;

	assert(shader->instrs_count);

	info->sizedwords    = 4 * shader->instrs_count;
	info->max_reg       = -1;
	info->max_input_reg = 0;
	info->regs_written  = 0;

	buffer = pipe_buffer_create(ctx->base.screen,
					PIPE_BIND_CUSTOM, PIPE_USAGE_IMMUTABLE,
					4 * info->sizedwords);
	if (!buffer) {
		ERROR_MSG("shader BO allocation failed");
		return NULL;
	}

	ptr = dwords = pipe_buffer_map(&ctx->base, buffer, PIPE_TRANSFER_WRITE,
					&transfer);
	if (!ptr) {
		ERROR_MSG("failed to map shader BO");
		goto fail;
	}

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

	if (transfer)
		pipe_buffer_unmap(&ctx->base, transfer);

	return buffer;

fail:
	if (transfer)
		pipe_buffer_unmap(&ctx->base, transfer);
	if (buffer)
		pipe_resource_reference(&buffer, NULL);
	return NULL;
}

struct of_ir_register *
of_ir_reg_create(struct of_ir_shader *shader, struct of_ir_instruction *instr,
		 int num, const char *swizzle, int flags, int type)
{
	struct of_ir_register *reg = of_ir_alloc(shader, sizeof(*reg));

	DEBUG_MSG("%x, %d, %s", flags, num, swizzle);
	assert(num <= REG_MASK);
	reg->flags = flags;
	reg->num = num;
	reg->swizzle = of_ir_strdup(shader, swizzle);
	reg->type = type;
	assert(instr->src_count < ARRAY_SIZE(instr->src));
	instr->src[instr->src_count++] = reg;

	if (instr->src_count == 4) {
		assert(shader->instrs_count > 1);

		instr = shader->instrs[shader->instrs_count - 2];
		instr->next_3arg = true;
	}

	return reg;
}
