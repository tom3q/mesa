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
		int offset = instr->target->end_address - pc;
		bool backwards = offset < 0;

		if (backwards) {
			dwords[2] |= CF_WORD2_JUMP_BACK;
			offset = -offset;
		}

		dwords[2] |= CF_WORD2_JUMP_OFFS(offset);
	}

	/* TODO: Implement predicate support */
}

static void
generate_code(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;
	struct of_ir_ast_node *child;

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list) {
		switch (child->type) {
		case OF_IR_NODE_LIST:
			LIST_FOR_EACH_ENTRY(ins, &child->list.instrs, list)
				instr_emit(opt->shader, ins, opt->dwords,
						opt->cur_instr++);
			continue;

		case OF_IR_NODE_IF_THEN:
			ins = of_ir_instr_create(opt->shader, OF_OP_BF);
			ins->target = child;
			instr_emit(opt->shader, ins, opt->dwords,
					opt->cur_instr++);
			break;

		default:
			break;
		}

		generate_code(opt, child);

		switch (child->type) {
		case OF_IR_NODE_DEPART:
		case OF_IR_NODE_REPEAT:
			if (child->depart_repeat.region->parent) {
				ins = of_ir_instr_create(opt->shader, OF_OP_B);
				ins->target = child->depart_repeat.region;
			} else {
				ins = of_ir_instr_create(opt->shader,
								OF_OP_RET);
			}
			instr_emit(opt->shader, ins, opt->dwords,
					opt->cur_instr++);
			break;

		default:
			break;
		}
	}
}

/*
 * A pass to collect stats needed to generate target bytecode.
 */

static void
collect_stats(struct of_ir_optimizer *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child;

	LIST_FOR_EACH_ENTRY(child, &node->nodes, parent_list) {
		node->start_address = opt->shader->stats.num_instrs;

		switch (child->type) {
		case OF_IR_NODE_LIST: {
			struct of_ir_instruction *ins;

			LIST_FOR_EACH_ENTRY(ins, &child->list.instrs, list)
				++opt->shader->stats.num_instrs;

			continue; }

		case OF_IR_NODE_IF_THEN:
			++opt->shader->stats.num_instrs;
			break;

		default:
			break;
		}

		collect_stats(opt, child);

		switch (child->type) {
		case OF_IR_NODE_DEPART:
		case OF_IR_NODE_REPEAT:
			++opt->shader->stats.num_instrs;
			break;

		default:
			break;
		}

		node->end_address = opt->shader->stats.num_instrs;
	}
}

/*
 * Bytecode generator entry point.
 */

int
of_ir_generate_code(struct of_context *ctx, struct of_ir_shader *shader)
{
	struct pipe_transfer *transfer;
	struct of_ir_optimizer *opt;
	struct of_heap *heap;
	uint32_t *dwords;

	heap = of_heap_create();
	opt = of_heap_alloc(heap, sizeof(*opt));
	opt->shader = shader;
	opt->heap = heap;

	shader->stats.num_instrs = 0;
	RUN_PASS(shader, opt, collect_stats);

	shader->buffer = pipe_buffer_create(ctx->base.screen,
					PIPE_BIND_CUSTOM, PIPE_USAGE_IMMUTABLE,
					4 * sizeof(*dwords)
					* shader->stats.num_instrs);
	if (!shader->buffer) {
		ERROR_MSG("shader BO allocation failed");
		return -1;
	}

	opt->dwords = pipe_buffer_map(&ctx->base, shader->buffer,
					PIPE_TRANSFER_WRITE, &transfer);
	if (!opt->dwords) {
		ERROR_MSG("failed to map shader BO");
		goto fail;
	}

	RUN_PASS(shader, opt, generate_code);

	if (transfer)
		pipe_buffer_unmap(&ctx->base, transfer);

	of_heap_destroy(heap);
	return 0;

fail:
	of_heap_destroy(heap);
	pipe_resource_reference(&shader->buffer, NULL);
	return -1;
}
