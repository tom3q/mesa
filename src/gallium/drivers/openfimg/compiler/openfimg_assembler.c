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

struct of_instr_bitfield {
	uint32_t mask;
	uint8_t shift;
	uint8_t word;
};

struct of_instr_flag {
	uint32_t instr;
	uint32_t ir;
	uint8_t word;
};

struct of_reg_bitfields {
	struct of_instr_bitfield num;
	struct of_instr_bitfield type;
	struct of_instr_bitfield mask;
	struct of_instr_flag flags[3];
};

static const enum of_ir_reg_type src_types[] = {
	[OF_SRC_V] = OF_IR_REG_V,
	[OF_SRC_R] = OF_IR_REG_R,
	[OF_SRC_C] = OF_IR_REG_C,
	[OF_SRC_I] = OF_IR_REG_I,
	[OF_SRC_AL] = OF_IR_REG_AL,
	[OF_SRC_B] = OF_IR_REG_B,
	[OF_SRC_P] = OF_IR_REG_P,
	[OF_SRC_S] = OF_IR_REG_S,
	[OF_SRC_D] = OF_IR_REG_D,
	[OF_SRC_VFACE] = OF_IR_REG_VFACE,
	[OF_SRC_VPOS] = OF_IR_REG_VPOS,
};

static const enum of_ir_reg_type dst_types[] = {
	[OF_DST_O] = OF_IR_REG_O,
	[OF_DST_R] = OF_IR_REG_R,
	[OF_DST_P] = OF_IR_REG_P,
	[OF_DST_A0] = OF_IR_REG_A0,
	[OF_DST_AL] = OF_IR_REG_AL,
};


static const struct of_reg_bitfields src_bitfields[] = {
	[0] = {
		.num = {
			.mask = INSTR_WORD1_SRC0_NUM__MASK,
			.shift = INSTR_WORD1_SRC0_NUM__SHIFT,
			.word = 1,
		},
		.type = {
			.mask = INSTR_WORD1_SRC0_TYPE__MASK,
			.shift = INSTR_WORD1_SRC0_TYPE__SHIFT,
			.word = 1,
		},
		.mask = {
			.mask = INSTR_WORD2_SRC0_SWIZZLE__MASK,
			.shift = INSTR_WORD2_SRC0_SWIZZLE__SHIFT,
			.word = 2,
		},
		.flags = {
			{
				.instr = INSTR_WORD1_SRC0_NEGATE,
				.ir = OF_IR_REG_NEGATE,
				.word = 1,
			}, {
				.instr = INSTR_WORD1_SRC0_ABS,
				.ir = OF_IR_REG_ABS,
				.word = 1,
			}
		}
	},
	[1] = {
		.num = {
			.mask = ALU_WORD0_SRC1_NUM__MASK,
			.shift = ALU_WORD0_SRC1_NUM__SHIFT,
			.word = 0,
		},
		.type = {
			.mask = ALU_WORD1_SRC1_TYPE__MASK,
			.shift = ALU_WORD1_SRC1_TYPE__SHIFT,
			.word = 1,
		},
		.mask = {
			.mask = ALU_WORD1_SRC1_SWIZZLE__MASK,
			.shift = ALU_WORD1_SRC1_SWIZZLE__SHIFT,
			.word = 1,
		},
		.flags = {
			{
				.instr = ALU_WORD1_SRC1_NEGATE,
				.ir = OF_IR_REG_NEGATE,
				.word = 1,
			}, {
				.instr = ALU_WORD1_SRC1_ABS,
				.ir = OF_IR_REG_ABS,
				.word = 1,
			}
		}
	},
	[2] = {
		.num = {
			.mask = ALU_WORD0_SRC2_NUM__MASK,
			.shift = ALU_WORD0_SRC2_NUM__SHIFT,
			.word = 0,
		},
		.type = {
			.mask = ALU_WORD0_SRC2_TYPE__MASK,
			.shift = ALU_WORD0_SRC2_TYPE__SHIFT,
			.word = 0,
		},
		.mask = {
			.mask = ALU_WORD0_SRC2_SWIZZLE__MASK,
			.shift = ALU_WORD0_SRC2_SWIZZLE__SHIFT,
			.word = 0,
		},
		.flags = {
			{
				.instr = ALU_WORD0_SRC2_NEGATE,
				.ir = OF_IR_REG_NEGATE,
				.word = 0,
			}, {
				.instr = ALU_WORD0_SRC2_ABS,
				.ir = OF_IR_REG_ABS,
				.word = 0,
			}
		}
	},
};

static const struct of_reg_bitfields dst_bitfields = {
	.num = {
		.mask = ALU_WORD2_DST_NUM__MASK,
		.shift = ALU_WORD2_DST_NUM__SHIFT,
		.word = 2,
	},
	.type = {
		.mask = ALU_WORD2_DST_TYPE__MASK,
		.shift = ALU_WORD2_DST_TYPE__SHIFT,
		.word = 2,
	},
	.mask = {
		.mask = ALU_WORD2_DST_MASK__MASK,
		.shift = ALU_WORD2_DST_MASK__SHIFT,
		.word = 2,
	},
	.flags = {
		{
			.instr = ALU_WORD2_DST_SAT,
			.ir = OF_IR_REG_SAT,
			.word = 2,
		}
	}
};

static INLINE unsigned
get_bitfield(const uint32_t *instr, const struct of_instr_bitfield *field)
{
	return (instr[field->word] & field->mask) >> field->shift;
}

static INLINE void
set_bitfield(unsigned val, uint32_t *instr,
	     const struct of_instr_bitfield *field)
{
	instr[field->word] &= ~field->mask;
	instr[field->word] |= (val << field->shift) & field->mask;
}

static INLINE unsigned
get_flags(const uint32_t *instr, const struct of_instr_flag *flags)
{
	unsigned val = 0;

	while (flags->instr) {
		if (instr[flags->word] & flags->instr)
			val |= flags->ir;
		++flags;
	}

	return val;
}

static INLINE void
set_flags(unsigned val, uint32_t *instr, const struct of_instr_flag *flags)
{
	while (flags->instr) {
		if (val & flags->ir)
			instr[flags->word] |= flags->instr;
		else
			instr[flags->word] &= ~flags->instr;
		++flags;
	}
}

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
	const struct of_ir_reg_info *info;
	struct of_ir_register *dst;
	unsigned i;

	memset(dwords, 0, 4 * sizeof(uint32_t));

	dwords[2] |= INSTR_WORD2_OPCODE(instr->opc);

	/* We rely on the fact that we insert NOPs on the beginning of
	 * every list which begins with a 3-source operation. */
	if (instr->num_srcs == 3) {
		uint32_t *prev = buffer + 4 * (pc - 1);

		prev[2] |= INSTR_WORD2_NEXT_3SRC;
	}

	for (i = 0; i < instr->num_srcs; ++i) {
		const struct of_reg_bitfields *bflds = &src_bitfields[i];
		struct of_ir_register *src = instr->srcs[i];

		info = of_ir_get_reg_info(shader, src->type);
		set_bitfield(src->num, dwords, &bflds->num);
		set_bitfield(info->src_type, dwords, &bflds->type);
		set_bitfield(src_swiz(src), dwords, &bflds->mask);
		set_flags(src->flags, dwords, bflds->flags);

		/* Here we rely on the fact that only one source can be
		 * a const float and this is the only type that has more
		 * than 32 registers. */
		if (src->num >= 32)
			dwords[1] |= INSTR_WORD1_SRC_EXTNUM(src->num / 32);
	}

	/* Destination register */
	dst = instr->dst;
	if (dst) {
		info = of_ir_get_reg_info(shader, dst->type);
		set_bitfield(dst->num, dwords, &dst_bitfields.num);
		set_bitfield(info->dst_type, dwords, &dst_bitfields.type);
		set_bitfield(dst_mask(dst), dwords, &dst_bitfields.mask);
		set_flags(dst->flags, dwords, dst_bitfields.flags);
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
			if (!LIST_IS_EMPTY(&child->list.instrs)) {
				ins = LIST_ENTRY(struct of_ir_instruction,
						child->list.instrs.next, list);
				if (ins->num_srcs == 3) {
					ins = of_ir_instr_create(opt->shader,
								OF_OP_NOP);
					instr_emit(opt->shader, ins,
						opt->dwords, opt->cur_instr++);
				}
			}
			LIST_FOR_EACH_ENTRY(ins, &child->list.instrs, list)
				instr_emit(opt->shader, ins, opt->dwords,
						opt->cur_instr++);
			continue;

		case OF_IR_NODE_IF_THEN:
			ins = of_ir_instr_create(opt->shader, OF_OP_BF);
			of_ir_instr_add_src(ins, child->if_then.reg);
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

			if (!LIST_IS_EMPTY(&child->list.instrs)) {
				ins = LIST_ENTRY(struct of_ir_instruction,
						child->list.instrs.next, list);
				if (ins->num_srcs == 3)
					++opt->shader->stats.num_instrs;
			}
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

/*
 * Disassembler.
 */

static void
disassemble_code(uint32_t *dwords, unsigned num_dwords,
		 enum of_shader_type type)
{
	struct of_ir_shader *shader = of_ir_shader_create(type);
	static const char mask_templ[] = "_x_y_z_w";
	struct of_ir_ast_node *list;
	unsigned i;

	list = of_ir_node_list(shader);

	for (i = 0; i < num_dwords; i += 4) {
		const struct of_ir_opc_info *info;
		unsigned num, type, mask, flags;
		struct of_ir_instruction *ins;
		uint32_t *instr = &dwords[i];
		struct of_ir_register *reg;
		char mask_str[4];
		unsigned opcode;
		unsigned comp;
		unsigned s;

		opcode = (instr[2] & INSTR_WORD2_OPCODE__MASK)
				>> INSTR_WORD2_OPCODE__SHIFT;
		if (opcode > OF_OP_RET)
			opcode = OF_OP_NOP;

		info = of_ir_get_opc_info(opcode);
		ins = of_ir_instr_create(shader, opcode);

		for (s = 0; s < info->num_srcs; ++s) {
			num = get_bitfield(instr, &src_bitfields[s].num);
			type = get_bitfield(instr, &src_bitfields[s].type);
			mask = get_bitfield(instr, &src_bitfields[s].mask);
			flags = get_flags(instr, src_bitfields[s].flags);

			for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
				unsigned idx = (mask >> (2 * comp)) & 3;

				mask_str[comp] = "xyzw"[idx];
			}

			reg = of_ir_reg_create(shader, src_types[type], num,
						mask_str, flags);
			of_ir_instr_add_src(ins, reg);
		}

		if (info->type == OF_IR_ALU) {
			num = get_bitfield(instr, &dst_bitfields.num);
			type = get_bitfield(instr, &dst_bitfields.type);
			mask = get_bitfield(instr, &dst_bitfields.mask);
			flags = get_flags(instr, dst_bitfields.flags);

			for (comp = 0; comp < OF_IR_VEC_SIZE; ++comp) {
				unsigned idx = 2 * comp + ((mask >> comp) & 1);

				mask_str[comp] = mask_templ[idx];
			}

			reg = of_ir_reg_create(shader, dst_types[type], num,
						mask_str, flags);
			of_ir_instr_add_dst(ins, reg);
		}

		of_ir_instr_insert(shader, list, NULL, ins);
	}

	of_ir_dump_ast(shader, NULL, NULL);

	of_ir_shader_destroy(shader);
}

int
of_ir_shader_disassemble(struct of_context *ctx, struct pipe_resource *buffer,
			 unsigned num_dwords, enum of_shader_type type)
{
	struct pipe_transfer *transfer;
	uint32_t *dwords;

	dwords = pipe_buffer_map(&ctx->base, buffer, PIPE_TRANSFER_WRITE,
					&transfer);
	if (!dwords)
		return -1;

	disassemble_code(dwords, num_dwords, type);

	if (transfer)
		pipe_buffer_unmap(&ctx->base, transfer);

	return 0;
}
