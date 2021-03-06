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

#include "openfimg_compiler.h"
#include "openfimg_ir_priv.h"
#include "openfimg_util.h"
#include "openfimg_texture.h"

struct of_ir_assembler {
	struct of_context *ctx;
	struct of_ir_shader *shader;
	uint32_t *dwords;
	unsigned cur_instr;
};

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

/*
 * Structures describing instruction word layout
 */

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

/*
 * Instruction word access helpers
 */

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
src_swiz(struct of_ir_register *reg)
{
	uint32_t swiz = 0x00;
	int i;

	for (i = 0; i < 4; ++i)
		swiz |= reg->swizzle[i] << 2 * i;

	return swiz;
}

static void
patch_texld(struct of_ir_assembler *opt, uint32_t *dwords)
{
	unsigned swizzle = get_bitfield(dwords, &src_bitfields[1].mask);
	unsigned sampler = get_bitfield(dwords, &src_bitfields[1].num);
	struct of_texture_stateobj *tex = &opt->ctx->fragtex;
	struct pipe_sampler_view *texture = tex->textures[sampler];
	struct of_pipe_sampler_view *of_texture = of_pipe_sampler_view(texture);
	static const uint8_t swizzle_map[4] = { 2, 1, 0, 3 };

	if (of_texture->swizzle)
		swizzle = swizzle_map[swizzle & 3]
			| swizzle_map[(swizzle >> 2) & 3] << 2
			| swizzle_map[(swizzle >> 4) & 3] << 4
			| swizzle_map[(swizzle >> 6) & 3] << 6;

	set_bitfield(swizzle, dwords, &src_bitfields[1].mask);
}

static void
instr_emit(struct of_ir_assembler *opt, struct of_ir_instruction *instr,
	   uint32_t *buffer, unsigned pc)
{
	struct of_ir_shader *shader = opt->shader;
	const struct of_ir_opc_info *opc_info;
	uint32_t *dwords = buffer + 4 * pc;
	const struct of_ir_reg_info *info;
	struct of_ir_register *dst;
	unsigned i;

	opc_info = of_ir_get_opc_info(instr->opc);

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
		set_bitfield(dst->mask, dwords, &dst_bitfields.mask);
		set_flags(dst->flags, dwords, dst_bitfields.flags);
	}

	if (opc_info->type == OF_IR_CF || opc_info->type == OF_IR_SUB) {
		int offset = instr->target - pc - 1;

		dwords[2] |= CF_WORD2_JUMP_OFFS((uint32_t)offset);
	}

	if (shader->type == OF_SHADER_PIXEL && instr->opc == OF_OP_TEXLD)
		patch_texld(opt, dwords);

	/* TODO: Implement predicate support */
}

/*
 * Main code generation pass.
 */

static void
generate_code(struct of_ir_assembler *opt, struct of_ir_ast_node *node)
{
	struct of_ir_instruction *ins;
	struct of_ir_ast_node *target;
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
					instr_emit(opt, ins,
						opt->dwords, opt->cur_instr++);
				}
			}
			LIST_FOR_EACH_ENTRY(ins, &child->list.instrs, list)
				instr_emit(opt, ins, opt->dwords,
						opt->cur_instr++);
			continue;

		case OF_IR_NODE_IF_THEN:
			ins = of_ir_instr_create(opt->shader, OF_OP_BF);
			of_ir_instr_add_src(ins, child->if_then.reg);
			ins->target = child->end_address;
			instr_emit(opt, ins, opt->dwords,
					opt->cur_instr++);
			break;

		default:
			break;
		}

		generate_code(opt, child);

		switch (child->type) {
		case OF_IR_NODE_REPEAT:
			target = child->depart_repeat.region;
			ins = of_ir_instr_create(opt->shader, OF_OP_B);
			ins->target = target->start_address;
			instr_emit(opt, ins, opt->dwords,
					opt->cur_instr++);
			break;
		case OF_IR_NODE_DEPART:
			target = child->depart_repeat.region;
			if (target->parent) {
				ins = of_ir_instr_create(opt->shader, OF_OP_B);
				ins->target = target->end_address;
			} else {
				ins = of_ir_instr_create(opt->shader,
								OF_OP_RET);
			}
			instr_emit(opt, ins, opt->dwords,
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
collect_stats(struct of_ir_assembler *opt, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node *child, *s;

	node->start_address = opt->shader->stats.num_instrs;

	LIST_FOR_EACH_ENTRY_SAFE(child, s, &node->nodes, parent_list) {
		switch (child->type) {
		case OF_IR_NODE_LIST: {
			struct of_ir_instruction *ins;

			if (!LIST_IS_EMPTY(&child->list.instrs)) {
				ins = LIST_ENTRY(struct of_ir_instruction,
						child->list.instrs.next, list);
				if (ins->num_srcs == 3)
					++opt->shader->stats.num_instrs;
			} else {
				list_del(&child->parent_list);
				continue;
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
		case OF_IR_NODE_REGION:
			if (LIST_IS_EMPTY(&child->nodes))
				list_del(&child->parent_list);
			break;
		default:
			break;
		}
	}

	node->end_address = opt->shader->stats.num_instrs;
}

/*
 * Bytecode generator entry point.
 */

int
of_ir_generate_code(struct of_context *ctx, struct of_ir_shader *shader,
		    struct pipe_resource **buffer, unsigned *num_instrs)
{
	struct pipe_transfer *transfer;
	struct of_ir_assembler opt;
	uint32_t *dwords;

	memset(&opt, 0, sizeof(opt));

	opt.ctx = ctx;
	opt.shader = shader;

	shader->stats.num_instrs = 0;
	RUN_PASS(shader, &opt, collect_stats);
	OF_IR_DUMP_AST(shader, NULL, 0, "pre-assembler");

	shader->buffer = pipe_buffer_create(ctx->base.screen,
					PIPE_BIND_CUSTOM, PIPE_USAGE_IMMUTABLE,
					4 * sizeof(*dwords)
					* shader->stats.num_instrs);
	if (!shader->buffer) {
		ERROR_MSG("shader BO allocation failed");
		return -1;
	}

	opt.dwords = pipe_buffer_map(&ctx->base, shader->buffer,
					PIPE_TRANSFER_WRITE, &transfer);
	if (!opt.dwords) {
		ERROR_MSG("failed to map shader BO");
		goto fail;
	}

	RUN_PASS(shader, &opt, generate_code);

	if (transfer)
		pipe_buffer_unmap(&ctx->base, transfer);

	*buffer = shader->buffer;
	*num_instrs = shader->stats.num_instrs;

	return 0;

fail:
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
		} else {
			uint32_t val = (instr[2] & CF_WORD2_JUMP_OFFS__MASK)
					>> CF_WORD2_JUMP_OFFS__SHIFT;
			int offset = val;

			if (val & BIT(8))
				offset = (int)(val | 0xfffffe00);

			ins->target = offset + i / 4 + 1;
		}

		of_ir_instr_insert(shader, list, NULL, ins);
	}

	of_ir_dump_ast(shader, NULL, NULL, "disassembler");

	of_ir_shader_destroy(shader);
}

int
of_shader_disassemble(struct of_context *ctx, struct pipe_resource *buffer,
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
