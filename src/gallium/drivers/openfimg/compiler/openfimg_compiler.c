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

#include "pipe/p_state.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_hash_table.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_ureg.h"
#include "tgsi/tgsi_info.h"
#include "tgsi/tgsi_strings.h"
#include "tgsi/tgsi_dump.h"

#include "openfimg_compiler.h"
#include "openfimg_program.h"
#include "openfimg_util.h"
#include "openfimg_ir.h"

#include "fimg_3dse.xml.h"

struct of_compile_context {
	const struct tgsi_token *tokens;

	struct tgsi_parse_context parser;
	unsigned type;

	uint8_t num_regs[TGSI_FILE_COUNT];

	int position;
	int psize;

	unsigned num_immediates;
	uint32_t immediates[1024];

	struct tgsi_declaration_semantic input_map[OF_MAX_ATTRIBS];
	struct tgsi_declaration_semantic output_map[OF_MAX_ATTRIBS];

	/* current shader */
	struct of_ir_shader *shader;

	struct of_stack *loop_stack;
	struct of_ir_ast_node *current_node;

	/* for subroutines */
	bool in_subroutine;
	struct of_ir_ast_node *prev_node;
	struct util_hash_table *subroutine_ht;
};

typedef void (*of_tgsi_opcode_handler_t)(struct of_compile_context *,
					 struct tgsi_full_instruction *,
					 unsigned long);

struct of_tgsi_map_entry {
	of_tgsi_opcode_handler_t handler;
	unsigned long handler_data;
};

typedef void (*token_handler_t)(struct of_compile_context *ctx);

/*
 * Constants used by code generators.
 */

static const float zero_one[2] = {
	0.0f, 1.0f
};

static const float sin_quad_constants[2][4] = {
	{
		2.0, -1.0, .5, .75
	}, {
		4.0, -4.0, 1.0 / (2.0 * M_PI), .2225
	}
};

static const float sin_constants[4] = {
	1.0,
	-1.0 / (3 * 2 * 1),
	1.0 / (5 * 4 * 3 * 2 * 1),
	-1.0 / (7 * 6 * 5 * 4 * 3 * 2 * 1)
};

/* 1, -1/2!, 1/4!, -1/6! */
static const float cos_constants[4] = {
	1.0,
	-1.0 / (2 * 1),
	1.0 / (4 * 3 * 2 * 1),
	-1.0 / (6 * 5 * 4 * 3 * 2 * 1)
};

/*
 * Helpers
 */

static unsigned
integer_hash(void *key)
{
	return (unsigned long)key;
}

static int
pointer_compare(void *key1, void *key2)
{
	return key1 != key2;
}

static void
compile_error(struct of_compile_context *ctx, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	_debug_vprintf(format, ap);
	va_end(ap);
	tgsi_dump(ctx->tokens, 0);
	debug_assert(0);
}

#define compile_assert(ctx, cond) do { \
		if (!(cond)) compile_error((ctx), "failed assert: "#cond"\n"); \
	} while (0)

static void
process_tokens(struct of_compile_context *ctx, const token_handler_t *handlers,
	       unsigned num_handlers)
{
	while (!tgsi_parse_end_of_tokens(&ctx->parser)) {
		token_handler_t handler;
		unsigned token_type;

		tgsi_parse_token(&ctx->parser);

		token_type = ctx->parser.FullToken.Token.Type;

		if (token_type >= num_handlers)
			continue;

		handler = handlers[token_type];
		if (!handler)
			continue;

		handler(ctx);
	}
}

/*
 * Code generation (TGSI to OF_IR).
 */

static struct of_ir_register *
get_dst_reg(struct of_compile_context *ctx, struct tgsi_full_instruction *inst)
{
	const struct tgsi_dst_register *dst = &inst->Dst[0].Register;
	enum of_ir_reg_type type;
	unsigned flags = 0;
	unsigned num;
	char swiz[5];

	switch (dst->File) {
	case TGSI_FILE_OUTPUT:
		type = OF_IR_REG_O;
		num = dst->Index;
		if (ctx->type == TGSI_PROCESSOR_FRAGMENT)
			flags |= OF_IR_REG_SAT;
		break;

	case TGSI_FILE_TEMPORARY:
		type = OF_IR_REG_VAR;
		num = dst->Index;
		break;

	case TGSI_FILE_ADDRESS:
		type = OF_IR_REG_AL;
		num = dst->Index;
		break;

	case TGSI_FILE_PREDICATE:
		type = OF_IR_REG_P;
		num = dst->Index;
		break;

	default:
		compile_error(ctx, "unsupported dst register file: %s\n",
				tgsi_file_name(dst->File));
		return NULL;
	}

	switch (inst->Instruction.Saturate) {
	case TGSI_SAT_NONE:
		break;
	case TGSI_SAT_ZERO_ONE:
		flags |= OF_IR_REG_SAT;
		break;
	case TGSI_SAT_MINUS_PLUS_ONE:
		compile_error(ctx, "unsupported saturate: %u\n",
				inst->Instruction.Saturate);
	}

	swiz[0] = (dst->WriteMask & TGSI_WRITEMASK_X) ? 'x' : '_';
	swiz[1] = (dst->WriteMask & TGSI_WRITEMASK_Y) ? 'y' : '_';
	swiz[2] = (dst->WriteMask & TGSI_WRITEMASK_Z) ? 'z' : '_';
	swiz[3] = (dst->WriteMask & TGSI_WRITEMASK_W) ? 'w' : '_';
	swiz[4] = '\0';

	return of_ir_reg_create(ctx->shader, type, num, swiz, flags);
}

static struct of_ir_register *
get_src_reg(struct of_compile_context *ctx, struct tgsi_full_instruction *inst,
	    unsigned src_num)
{
	const struct tgsi_src_register *src = &inst->Src[src_num].Register;
	static const char swiz_vals[] = "xyzw";
	enum of_ir_reg_type type;
	unsigned flags = 0;
	unsigned num;
	char swiz[5];

	switch (src->File) {
	case TGSI_FILE_CONSTANT:
		num = src->Index;
		type = OF_IR_REG_C;
		break;

	case TGSI_FILE_INPUT:
		num = src->Index;
		type = OF_IR_REG_V;
		break;

	case TGSI_FILE_TEMPORARY:
		num = src->Index;
		type = OF_IR_REG_VAR;
		break;

	case TGSI_FILE_IMMEDIATE:
		num = src->Index + ctx->num_regs[TGSI_FILE_CONSTANT];
		type = OF_IR_REG_C;
		break;

	case TGSI_FILE_SAMPLER:
		num = src->Index;
		type = OF_IR_REG_S;
		break;

	default:
		compile_error(ctx, "unsupported src register file: %s\n",
				tgsi_file_name(src->File));
		return NULL;
	}

	if (src->Absolute)
		flags |= OF_IR_REG_ABS;
	if (src->Negate)
		flags |= OF_IR_REG_NEGATE;

	swiz[0] = swiz_vals[src->SwizzleX];
	swiz[1] = swiz_vals[src->SwizzleY];
	swiz[2] = swiz_vals[src->SwizzleZ];
	swiz[3] = swiz_vals[src->SwizzleW];
	swiz[4] = '\0';

	return of_ir_reg_create(ctx->shader, type, num, swiz, flags);
}

static struct of_ir_register *
get_temporary(struct of_compile_context *ctx)
{
	return of_ir_reg_create(ctx->shader, OF_IR_REG_VAR,
				ctx->num_regs[TGSI_FILE_TEMPORARY]++,
				"xyzw", 0);
}

static struct of_ir_register *
get_predicate(struct of_compile_context *ctx)
{
	/* TODO: Support for remaining predicate registers. */
	return of_ir_reg_create(ctx->shader, OF_IR_REG_P, 0, "xyzw", 0);
}

/* TODO: Implement immediate coalescing. */
static struct of_ir_register *
get_immediate(struct of_compile_context *ctx, unsigned dim, const float *vals)
{
	unsigned offset = ctx->num_immediates % 4;
	unsigned free_in_slot = 4 - offset;
	unsigned ptr = ctx->num_immediates;
	char swizzle[4] = "xxxx";
	unsigned i;

	assert(dim <= 4);

	if (free_in_slot < dim) {
		ptr += free_in_slot;
		offset = 0;
	}

	memcpy(&ctx->immediates[ptr], vals, dim * sizeof(*vals));
	ctx->num_immediates = ptr + dim;

	for (i = 0; i < dim; ++i)
		swizzle[i] = "xyzw"[offset + i];
	for (i = dim; i < 4; ++i)
		swizzle[i] = swizzle[dim - 1];

	return of_ir_reg_create(ctx->shader, OF_IR_REG_C,
		ctx->num_regs[TGSI_FILE_CONSTANT] + ptr / 4, swizzle, 0);
}

/*
 * Node stack
 */

static INLINE void
node_stack_push(struct of_stack *stack, struct of_ir_ast_node *node)
{
	struct of_ir_ast_node **ptr = of_stack_push(stack);

	*ptr = node;
}

static INLINE struct of_ir_ast_node *
node_stack_pop(struct of_stack *stack)
{
	struct of_ir_ast_node **ptr = of_stack_pop(stack);

	return *ptr;
}

static INLINE struct of_ir_ast_node *
node_stack_top(struct of_stack *stack)
{
	struct of_ir_ast_node **ptr = of_stack_top(stack);

	return *ptr;
}

/*
 * Helpers for TGSI instructions that don't map to a single shader instr:
 */

/* POW(a,b) = EXP2(b * LOG2(a)) */
static void
translate_pow(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_instr_template instrs[3];

	memset(instrs, 0, sizeof(instrs));

	/* dst = log(src0.xxxx) */
	instrs[0].opc = OF_OP_LOG;
	instrs[0].dst.reg = get_dst_reg(ctx, inst);
	instrs[0].dst.mask = "x___";
	instrs[0].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[0].src[0].swizzle = "xxxx";

	/* dst = dst.xxxx * src1.xxxx */
	instrs[1].opc = OF_OP_MUL;
	instrs[1].dst.reg = of_ir_reg_clone(ctx->shader, instrs[0].dst.reg);
	instrs[1].dst.mask = "x___";
	instrs[1].src[0].reg = of_ir_reg_clone(ctx->shader, instrs[0].dst.reg);
	instrs[1].src[0].swizzle = "xxxx";
	instrs[1].src[1].reg = get_src_reg(ctx, inst, 1);
	instrs[1].src[1].swizzle = "xxxx";

	/* dst = exp(dst.xxxx) */
	instrs[2].opc = OF_OP_EXP;
	instrs[2].dst.reg = of_ir_reg_clone(ctx->shader, instrs[1].dst.reg);
	instrs[2].src[0].reg = of_ir_reg_clone(ctx->shader, instrs[1].dst.reg);
	instrs[2].src[0].swizzle = "xxxx";

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node, NULL,
					instrs, ARRAY_SIZE(instrs));
}

static void
translate_tex(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_register *coord = NULL;
	struct of_ir_instr_template instr;

	switch (inst->Texture.Texture) {
	case TGSI_TEXTURE_2D:
	case TGSI_TEXTURE_RECT:
	case TGSI_TEXTURE_3D:
	case TGSI_TEXTURE_CUBE:
		break;
	default:
		compile_error(ctx, "unknown texture type: %s\n",
				tgsi_texture_names[inst->Texture.Texture]);
		return;
	}

	if (inst->Instruction.Opcode == TGSI_OPCODE_TXP) {
		/*
		 * TXP - Projective Texture Lookup:
		 *
		 * coord.x = src0.x / src.w
		 * coord.y = src0.y / src.w
		 * coord.z = src0.z / src.w
		 * coord.w = src0.w
		 */
		struct of_ir_instr_template proj[2];

		memset(proj, 0, sizeof(proj));

		/* tmp.x___ = 1 / src0.wwww */
		proj[0].opc = OF_OP_RCP;
		proj[0].dst.reg = get_temporary(ctx);
		proj[0].dst.mask = "x___";
		proj[0].src[0].reg = get_src_reg(ctx, inst, 0);
		proj[0].src[0].swizzle = "wwww";

		/* tmp.xyz_ = src0.xyzz * tmp.xxxx */
		proj[1].opc = OF_OP_MUL;
		proj[1].dst.reg = of_ir_reg_clone(ctx->shader, proj[0].dst.reg);
		proj[1].dst.mask = "xyz_";
		proj[1].src[0].reg = of_ir_reg_clone(ctx->shader,
							proj[0].src[0].reg);
		proj[1].src[0].swizzle = "xyzz";
		proj[1].src[1].reg = of_ir_reg_clone(ctx->shader,
							proj[0].dst.reg);
		proj[1].src[1].swizzle = "xxxx";

		of_ir_instr_insert_templ(ctx->shader, ctx->current_node, NULL,
						proj, ARRAY_SIZE(proj));

		coord = of_ir_reg_clone(ctx->shader, proj[1].dst.reg);
	}

	assert(inst->Texture.NumOffsets <= 1); // TODO what to do in other cases?

	if (!coord)
		coord = get_src_reg(ctx, inst, 0);

	memset(&instr, 0, sizeof(instr));

	instr.opc = OF_OP_TEXLD;
	instr.dst.reg = get_dst_reg(ctx, inst);
	instr.src[0].reg = coord;
	instr.src[1].reg = get_src_reg(ctx, inst, 1);

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
					NULL, &instr, 1);
}

/* LRP = (src0 * src1) + ((1 - src0) * src2) */
static void
translate_lrp(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_instr_template instrs[2];

	memset(instrs, 0, sizeof(instrs));

	/* tmp = -((1 - src0) * src2) = -src2 + src0 * src2 */
	instrs[0].opc = OF_OP_MAD;
	instrs[0].dst.reg = get_temporary(ctx);
	instrs[0].src[0].reg = get_src_reg(ctx, inst, 2);
	instrs[0].src[1].reg = get_src_reg(ctx, inst, 0);
	instrs[0].src[2].reg = get_src_reg(ctx, inst, 2);
	instrs[0].src[2].flags = OF_IR_REG_NEGATE;

	/* dst = src0 * src1 + -tmp */
	instrs[1].opc = OF_OP_MAD;
	instrs[1].dst.reg = get_dst_reg(ctx, inst);
	instrs[1].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[1].src[1].reg = get_src_reg(ctx, inst, 1);
	instrs[1].src[2].reg = of_ir_reg_clone(ctx->shader, instrs[0].dst.reg);
	instrs[1].src[2].flags = OF_IR_REG_NEGATE;

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node, NULL,
					instrs, ARRAY_SIZE(instrs));
}

static void
translate_trig(struct of_compile_context *ctx,
	       struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_instr_template instrs[9];
	struct of_ir_register *tmp, *consts0, *consts1;

	memset(instrs, 0, sizeof(instrs));

	/*
	 * Reduce range from repeating about [-pi,pi] to [-1,1]
	 */

	/* tmp.xz = src0.xx * consts1.zz + consts0.zw */
	instrs[0].opc = OF_OP_MAD;
	instrs[0].dst.reg = tmp = get_temporary(ctx);
	instrs[0].dst.mask = "x_z_";
	instrs[0].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[0].src[0].swizzle = "xxxx";
	instrs[0].src[1].reg = consts1 =
				get_immediate(ctx, 4, sin_quad_constants[1]);
	instrs[0].src[1].swizzle = "zzzz";
	instrs[0].src[2].reg = consts0 =
				get_immediate(ctx, 4, sin_quad_constants[0]);
	instrs[0].src[2].swizzle = "zzww";

	/* tmp.xz = {tmp.xz} */
	instrs[1].opc = OF_OP_FRC;
	instrs[1].dst.reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[1].dst.mask = "x_z_";
	instrs[1].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[1].src[0].swizzle = "xxzz";

	/* tmp.xz = tmp.xz * consts0.xx + consts0.yy */
	instrs[2].opc = OF_OP_MAD;
	instrs[2].dst.reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[2].dst.mask = "x_z_";
	instrs[2].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[2].src[0].swizzle = "xxzz";
	instrs[2].src[1].reg = of_ir_reg_clone(ctx->shader, consts0);
	instrs[2].src[1].swizzle = "xxxx";
	instrs[2].src[2].reg = of_ir_reg_clone(ctx->shader, consts0);
	instrs[2].src[2].swizzle = "yyyy";

	/*
	 * Compute sin/cos using a quadratic and quartic.  It gives continuity
	 * that repeating the Taylor series lacks every 2*pi, and has reduced
	 * error.
	 *
	 * Idea borrowed from Intel i915 (classic) driver.
	 */

	/* tmp.yw = tmp.xz * tmp.xz */
	instrs[3].opc = OF_OP_MUL;
	instrs[3].dst.reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[3].dst.mask = "_y_w";
	instrs[3].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[3].src[0].swizzle = "xxzz";
	instrs[3].src[1].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[3].src[1].swizzle = "xxzz";
	instrs[3].src[1].flags = OF_IR_REG_ABS;

	/* tmp = tmp * consts1.xyxy */
	instrs[4].opc = OF_OP_MUL;
	instrs[4].dst.reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[4].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[4].src[1].reg = of_ir_reg_clone(ctx->shader, consts1);
	instrs[4].src[1].swizzle = "xyxy";

	/* tmp.xz = tmp.xz + tmp.yw */
	instrs[5].opc = OF_OP_ADD;
	instrs[5].dst.reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[5].dst.mask = "x_z_";
	instrs[5].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[5].src[1].swizzle = "xxzz";
	instrs[5].src[1].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[5].src[1].swizzle = "yyww";

	/* tmp.yw = tmp.xz * |tmp.xz| - tmp.xz */
	instrs[6].opc = OF_OP_MAD;
	instrs[6].dst.reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[6].dst.mask = "_y_w";
	instrs[6].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[6].src[0].swizzle = "xxzz";
	instrs[6].src[1].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[6].src[1].swizzle = "xxzz";
	instrs[6].src[1].flags = OF_IR_REG_ABS;
	instrs[6].src[2].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[6].src[2].swizzle = "xxzz";
	instrs[6].src[2].flags = OF_IR_REG_NEGATE;

	/* See below... */
	instrs[7].opc = OF_OP_MAD;
	instrs[7].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[7].src[1].reg = of_ir_reg_clone(ctx->shader, consts1);
	instrs[7].src[1].swizzle = "wwww";
	instrs[7].src[2].reg = of_ir_reg_clone(ctx->shader, tmp);

	switch (inst->Instruction.Opcode) {
	case TGSI_OPCODE_SIN:
		/* dst = tmp.yyyy * consts1.wwww + tmp.xxxx */
		instrs[7].dst.reg = get_dst_reg(ctx, inst);
		instrs[7].src[0].swizzle = "yyyy";
		instrs[7].src[2].swizzle = "xxxx";

		of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
						NULL, instrs, 8);
		break;

	case TGSI_OPCODE_COS:
		/* dst = tmp.wwww * consts1.wwww + tmp.zzzz */
		instrs[7].dst.reg = get_dst_reg(ctx, inst);
		instrs[7].src[0].swizzle = "wwww";
		instrs[7].src[2].swizzle = "zzzz";

		of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
						NULL, instrs, 8);
		break;

	case TGSI_OPCODE_SCS:
		/* tmp.xy = tmp.wy * consts1.ww + tmp.zx */
		instrs[7].dst.reg = of_ir_reg_clone(ctx->shader, tmp);
		instrs[7].dst.mask = "xy__";
		instrs[7].src[0].swizzle = "wyyy";
		instrs[7].src[2].swizzle = "zxxx";

		/* tmp.xz = (0.0, 1.0) */
		instrs[8].opc = OF_OP_MOV;
		instrs[8].dst.reg = of_ir_reg_clone(ctx->shader, tmp);
		instrs[8].dst.mask = "__zw";
		instrs[8].src[0].reg = get_immediate(ctx, 2, zero_one);
		instrs[8].src[0].swizzle = "xxxy";

		of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
						NULL, instrs, 9);
		break;
	}
}

static void
translate_lit(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	const float constvals1[2] = { 0.0f, -128.0f };
	struct of_ir_instr_template instrs[7];
	struct of_ir_register *tmp, *consts1;

	memset(instrs, 0, sizeof(instrs));

	instrs[0].opc = OF_OP_MAX;
	instrs[0].dst.reg = tmp = get_temporary(ctx);
	instrs[0].dst.mask = "xy_w";
	instrs[0].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[0].src[0].swizzle = "xyyw";
	instrs[0].src[1].reg = consts1 = get_immediate(ctx, 2, constvals1);
	instrs[0].src[1].swizzle = "xxxy";

	instrs[1].opc = OF_OP_MIN;
	instrs[1].dst.reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[1].dst.mask = "___w";
	instrs[1].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[1].src[0].swizzle = "wwww";
	instrs[1].src[1].reg = of_ir_reg_clone(ctx->shader, consts1);
	instrs[1].src[1].swizzle = "yyyy";
	instrs[1].src[1].flags = OF_IR_REG_NEGATE;

	instrs[2].opc = OF_OP_LOG_LIT;
	instrs[2].dst.reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[2].dst.mask = "_y__";
	instrs[2].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[2].src[0].swizzle = "yyyy";

	instrs[3].opc = OF_OP_MUL_LIT;
	instrs[3].dst.reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[3].dst.mask = "_y__";
	instrs[3].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[3].src[0].swizzle = "wwww";
	instrs[3].src[1].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[3].src[1].swizzle = "yyyy";

	instrs[4].opc = OF_OP_EXP_LIT;
	instrs[4].dst.reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[4].dst.mask = "_y__";
	instrs[4].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[4].src[0].swizzle = "yyyy";

	instrs[5].opc = OF_OP_CMP;
	instrs[5].dst.reg = get_dst_reg(ctx, inst);
	instrs[5].dst.mask = "_yz_";
	instrs[5].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[5].src[0].swizzle = "xxxx";
	instrs[5].src[0].flags = OF_IR_REG_NEGATE;
	instrs[5].src[1].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[5].src[1].swizzle = "xxyy";
	instrs[5].src[2].reg = of_ir_reg_clone(ctx->shader, consts1);
	instrs[5].src[2].swizzle = "xxxx";

	instrs[6].opc = OF_OP_SGE;
	instrs[6].dst.reg = get_dst_reg(ctx, inst);
	instrs[6].dst.mask = "x__w";
	instrs[6].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[6].src[0].swizzle = "xxxx";
	instrs[6].src[1].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[6].src[1].swizzle = "xxxx";

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node, NULL,
					instrs, ARRAY_SIZE(instrs));
}

static void
translate_sub(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_instr_template instr;

	memset(&instr, 0, sizeof(instr));

	instr.opc = OF_OP_ADD;
	instr.dst.reg = get_dst_reg(ctx, inst);
	instr.src[0].reg = get_src_reg(ctx, inst, 0);
	instr.src[1].reg = get_src_reg(ctx, inst, 1);
	instr.src[1].flags = OF_IR_REG_NEGATE;

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
					NULL, &instr, 1);
}

static void
translate_clamp(struct of_compile_context *ctx,
		struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_instr_template instrs[2];
	struct of_ir_register *tmp;

	memset(instrs, 0, sizeof(instrs));

	instrs[0].opc = OF_OP_MAX;
	instrs[0].dst.reg = tmp = get_temporary(ctx);
	instrs[0].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[0].src[1].reg = get_src_reg(ctx, inst, 1);

	instrs[1].opc = OF_OP_MIN;
	instrs[1].dst.reg = get_dst_reg(ctx, inst);
	instrs[1].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[1].src[1].reg = get_src_reg(ctx, inst, 2);

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node, NULL,
					instrs, ARRAY_SIZE(instrs));
}

static void
translate_round(struct of_compile_context *ctx,
		struct tgsi_full_instruction *inst, unsigned long data)
{
	const float const_half = 0.5f;
	struct of_ir_instr_template instrs[4];
	struct of_ir_register *tmp1, *tmp2;

	memset(instrs, 0, sizeof(instrs));

	instrs[0].opc = OF_OP_ADD;
	instrs[0].dst.reg = tmp1 = get_temporary(ctx);
	instrs[0].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[0].src[0].flags = OF_IR_REG_ABS;
	instrs[0].src[1].reg = get_immediate(ctx, 1, &const_half);
	instrs[0].src[1].swizzle = "xxxx";

	instrs[1].opc = OF_OP_FRC;
	instrs[1].dst.reg = tmp2 = get_temporary(ctx);
	instrs[1].src[0].reg = of_ir_reg_clone(ctx->shader, tmp1);

	instrs[2].opc = OF_OP_ADD;
	instrs[2].dst.reg = of_ir_reg_clone(ctx->shader, tmp2);
	instrs[2].src[0].reg = of_ir_reg_clone(ctx->shader, tmp1);
	instrs[2].src[1].reg = of_ir_reg_clone(ctx->shader, tmp2);
	instrs[2].src[1].flags = OF_IR_REG_NEGATE;

	instrs[3].opc = OF_OP_CMP;
	instrs[3].dst.reg = get_dst_reg(ctx, inst);
	instrs[3].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[3].src[1].reg = of_ir_reg_clone(ctx->shader, tmp2);
	instrs[3].src[1].flags = OF_IR_REG_NEGATE;
	instrs[3].src[2].reg = of_ir_reg_clone(ctx->shader, tmp2);

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node, NULL,
					instrs, ARRAY_SIZE(instrs));
}

static void
translate_xpd(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	const float const_one = 1.0f;
	struct of_ir_instr_template instrs[3];
	struct of_ir_register *tmp;

	memset(instrs, 0, sizeof(instrs));

	instrs[0].opc = OF_OP_MUL;
	instrs[0].dst.reg = tmp = get_temporary(ctx);
	instrs[0].dst.mask = "xyz_";
	instrs[0].src[0].reg = get_src_reg(ctx, inst, 1);
	instrs[0].src[0].swizzle = "yzxx";
	instrs[0].src[1].reg = get_src_reg(ctx, inst, 0);
	instrs[0].src[1].swizzle = "zxyy";

	instrs[1].opc = OF_OP_MAD;
	instrs[1].dst.reg = get_dst_reg(ctx, inst);
	instrs[1].dst.mask = "xyz_";
	instrs[1].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[1].src[0].swizzle = "yzxx";
	instrs[1].src[1].reg = get_src_reg(ctx, inst, 1);
	instrs[1].src[1].swizzle = "zxyy";
	instrs[1].src[2].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[1].src[2].swizzle = "xyzz";
	instrs[1].src[2].flags = OF_IR_REG_NEGATE;

	instrs[2].opc = OF_OP_MOV;
	instrs[2].dst.reg = get_dst_reg(ctx, inst);
	instrs[2].dst.mask = "___w";
	instrs[2].src[0].reg = get_immediate(ctx, 1, &const_one);
	instrs[2].src[0].swizzle = "xxxx";

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node, NULL,
					instrs, ARRAY_SIZE(instrs));
}

static void
translate_abs(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_instr_template instr;

	memset(&instr, 0, sizeof(instr));

	instr.opc = OF_OP_MOV;
	instr.dst.reg = get_dst_reg(ctx, inst);
	instr.src[0].reg = get_src_reg(ctx, inst, 0);
	instr.src[0].flags = OF_IR_REG_ABS;

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
					NULL, &instr, 1);
}

static void
translate_ssg(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_instr_template instrs[2];
	struct of_ir_register *tmp, *const0;

	memset(instrs, 0, sizeof(instrs));

	instrs[0].opc = OF_OP_CMP;
	instrs[0].dst.reg = tmp = get_temporary(ctx);
	instrs[0].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[0].src[1].reg = const0 = get_immediate(ctx, 2, zero_one);
	instrs[0].src[1].swizzle = "yyyy";
	instrs[0].src[1].flags = OF_IR_REG_NEGATE;
	instrs[0].src[2].reg = of_ir_reg_clone(ctx->shader, const0);
	instrs[0].src[2].swizzle = "xxxx";

	instrs[1].opc = OF_OP_CMP;
	instrs[1].dst.reg = get_dst_reg(ctx, inst);
	instrs[1].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[1].src[0].flags = OF_IR_REG_NEGATE;
	instrs[1].src[1].reg = of_ir_reg_clone(ctx->shader, const0);
	instrs[1].src[1].swizzle = "yyyy";
	instrs[1].src[2].reg = of_ir_reg_clone(ctx->shader, tmp);

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
					NULL, instrs, ARRAY_SIZE(instrs));
}

static void
translate_sne_seq(struct of_compile_context *ctx,
		  struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_instr_template instrs[3];
	struct of_ir_register *tmp, *const0;

	memset(instrs, 0, sizeof(instrs));

	instrs[0].opc = OF_OP_ADD;
	instrs[0].dst.reg = tmp = get_temporary(ctx);
	instrs[0].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[0].src[1].reg = get_src_reg(ctx, inst, 1);
	instrs[0].src[1].flags = OF_IR_REG_NEGATE;

	instrs[1].opc = OF_OP_CMP;
	instrs[1].dst.reg = get_dst_reg(ctx, inst);
	instrs[1].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[1].src[1].reg = const0 = get_immediate(ctx, 2, zero_one);
	instrs[1].src[2].reg = of_ir_reg_clone(ctx->shader, const0);
	if (inst->Instruction.Opcode == TGSI_OPCODE_SNE) {
		instrs[1].src[1].swizzle = "yyyy";
		instrs[1].src[2].swizzle = "xxxx";
	} else /* if (inst->Instruction->Opcode == TGSI_OPCODE_SEQ) */ {
		instrs[1].src[1].swizzle = "xxxx";
		instrs[1].src[2].swizzle = "yyyy";
	}

	instrs[2].opc = OF_OP_CMP;
	instrs[2].dst.reg = get_dst_reg(ctx, inst);
	instrs[2].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[2].src[0].flags = OF_IR_REG_NEGATE;
	instrs[2].src[1].reg = of_ir_reg_clone(ctx->shader, const0);
	instrs[2].src[2].reg = get_dst_reg(ctx, inst);
	if (inst->Instruction.Opcode == TGSI_OPCODE_SNE)
		instrs[2].src[1].swizzle = "yyyy";
	else /* if (inst->Instruction->Opcode == TGSI_OPCODE_SEQ) */
		instrs[2].src[1].swizzle = "xxxx";

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
					NULL, instrs, ARRAY_SIZE(instrs));
}

static void
translate_dp2(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	const float const_zero = 0.0f;
	struct of_ir_instr_template instr;

	memset(&instr, 0, sizeof(instr));

	instr.opc = OF_OP_DP2ADD;
	instr.dst.reg = get_dst_reg(ctx, inst);
	instr.src[0].reg = get_src_reg(ctx, inst, 0);
	instr.src[1].reg = get_src_reg(ctx, inst, 1);
	instr.src[2].reg = get_immediate(ctx, 1, &const_zero);
	instr.src[2].swizzle = "xxxx";

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
					NULL, &instr, 1);
}

static void
translate_ddx(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_ddy(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_trunc(struct of_compile_context *ctx,
		struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_instr_template instrs[2];
	struct of_ir_register *tmp;

	memset(instrs, 0, sizeof(instrs));

	instrs[0].opc = OF_OP_FLR;
	instrs[0].dst.reg = tmp = get_temporary(ctx);
	instrs[0].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[0].src[0].flags = OF_IR_REG_ABS;

	instrs[1].opc = OF_OP_CMP;
	instrs[1].dst.reg = get_dst_reg(ctx, inst);
	instrs[1].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[1].src[1].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[1].src[1].flags = OF_IR_REG_NEGATE;
	instrs[1].src[2].reg = of_ir_reg_clone(ctx->shader, tmp);

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
					NULL, instrs, ARRAY_SIZE(instrs));
}

static void
translate_ceil(struct of_compile_context *ctx,
	       struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_instr_template instrs[2];
	struct of_ir_register *tmp;

	memset(instrs, 0, sizeof(instrs));

	instrs[0].opc = OF_OP_FLR;
	instrs[0].dst.reg = tmp = get_temporary(ctx);
	instrs[0].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[0].src[0].flags = OF_IR_REG_NEGATE;

	instrs[1].opc = OF_OP_MOV;
	instrs[1].dst.reg = get_dst_reg(ctx, inst);
	instrs[1].src[0].reg = of_ir_reg_clone(ctx->shader, tmp);
	instrs[1].src[0].flags = OF_IR_REG_NEGATE;

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
					NULL, instrs, ARRAY_SIZE(instrs));
}

static void
translate_kill(struct of_compile_context *ctx,
		  struct tgsi_full_instruction *inst, unsigned long data)
{
	const float const_one = 1.0f;
	struct of_ir_instr_template instr;

	memset(&instr, 0, sizeof(instr));

	instr.opc = OF_OP_TEXKILL;
	instr.src[0].reg = get_immediate(ctx, 1, &const_one);
	instr.src[0].swizzle = "xxxx";
	instr.src[0].flags = OF_IR_REG_NEGATE;

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
					NULL, &instr, 1);
}

/*
 * Dynamic flow control
 */
static void
translate_if(struct of_compile_context *ctx,
	     struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_instr_template instrs[1];
	const float const_zero = 0.0f;
	struct of_ir_register *pred;
	struct of_ir_ast_node *parent, *region, *if_then, *depart, *list;

	/* Emit condition check. */
	memset(instrs, 0, sizeof(instrs));
	instrs[0].opc = OF_OP_SETP_EQ;
	instrs[0].dst.reg = pred = get_predicate(ctx);
	instrs[0].dst.mask = "x___";
	instrs[0].src[0].reg = get_src_reg(ctx, inst, 0);
	instrs[0].src[0].swizzle = "xxxx";
	instrs[0].src[1].reg = get_immediate(ctx, 1, &const_zero);
	instrs[0].src[1].swizzle = "xxxx";
	of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
					NULL, instrs, ARRAY_SIZE(instrs));

	parent = of_ir_node_get_parent(ctx->current_node);

	/* Create region node surrounding the whole if-(else-)endif. */
	region = of_ir_node_region(ctx->shader);
	of_ir_node_insert(parent, region);

	/* Create if_then node for statements executed if condition is met. */
	if_then = of_ir_node_if_then(ctx->shader,
					of_ir_reg_clone(ctx->shader, pred),
					"xxxx", 0);
	of_ir_node_insert(region, if_then);

	/* Create depart node so rest of region can be used for else. */
	depart = of_ir_node_depart(ctx->shader, region);
	of_ir_node_insert(if_then, depart);

	list = of_ir_node_list(ctx->shader);
	of_ir_node_insert(depart, list);

	ctx->current_node = list;
}

static void
translate_else(struct of_compile_context *ctx,
	       struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_ast_node *region, *list, *depart, *if_then;

	/*
	 * Go up the tree to get our region node.
	 */
	depart = of_ir_node_get_parent(ctx->current_node);
	if_then = of_ir_node_get_parent(depart);
	region = of_ir_node_get_parent(if_then);

	/* Emit further instructions to a list directly in the region. */
	list = of_ir_node_list(ctx->shader);
	of_ir_node_insert(region, list);
	ctx->current_node = list;
}

static void
translate_endif(struct of_compile_context *ctx,
		struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_ast_node *node, *parent, *list;

	/* Jump above the list node and check type of found node. */
	node = of_ir_node_get_parent(ctx->current_node);
	if (of_ir_node_get_type(node) == OF_IR_NODE_DEPART) {
		/*
		 * Jump above depart and if_then as well, to get to the region.
		 */
		node = of_ir_node_get_parent(node);
		node = of_ir_node_get_parent(node);
	}

	parent = of_ir_node_get_parent(node);

	/* Emit further instructions to a list directly in the parent. */
	list = of_ir_node_list(ctx->shader);
	of_ir_node_insert(parent, list);
	ctx->current_node = list;
}

/*
 * Subroutines.
 */

static void
save_subroutine(struct of_compile_context *ctx, struct of_ir_ast_node *node)
{
	util_hash_table_set(ctx->subroutine_ht,
				(void *)(unsigned long)ctx->parser.Position,
				node);
}

static struct of_ir_ast_node *
find_subroutine(struct of_compile_context *ctx, unsigned label)
{
	struct of_ir_ast_node *node;

	node = util_hash_table_get(ctx->subroutine_ht,
					(void *)(unsigned long)label);
	assert(node);

	return node;
}

static void
translate_bgnsub(struct of_compile_context *ctx,
		 struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_ast_node *region, *list;

	assert(!ctx->in_subroutine);
	ctx->in_subroutine = true;
	ctx->prev_node = ctx->current_node;

	region = of_ir_node_region(ctx->shader);
	save_subroutine(ctx, region);

	/* Emit further instructions to a list directly in the parent. */
	list = of_ir_node_list(ctx->shader);
	of_ir_node_insert(region, list);
	ctx->current_node = list;
}

static void
translate_endsub(struct of_compile_context *ctx,
		 struct tgsi_full_instruction *inst, unsigned long data)
{
	assert(ctx->in_subroutine);
	ctx->in_subroutine = false;
	ctx->current_node = ctx->prev_node;
}

static void
translate_cal(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_instr_template instr;

	memset(&instr, 0, sizeof(instr));

	instr.opc = OF_OP_CALL;
	instr.target.node = find_subroutine(ctx, inst->Label.Label);

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
					NULL, &instr, 1);
}

/*
 * Loops.
 */
static void
translate_bgnloop(struct of_compile_context *ctx,
		  struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_ast_node *parent, *region, *repeat, *list;

	parent = of_ir_node_get_parent(ctx->current_node);

	/* Create loop region and push it both to CF and loop stack. */
	region = of_ir_node_region(ctx->shader);
	of_ir_node_insert(parent, region);
	node_stack_push(ctx->loop_stack, region);

	/* Insert repeat node to jump again to entry point of loop region. */
	repeat = of_ir_node_repeat(ctx->shader, region);
	of_ir_node_insert(region, repeat);

	/* Create instruction list node. */
	list = of_ir_node_list(ctx->shader);
	of_ir_node_insert(repeat, list);
	ctx->current_node = list;
}

static void
translate_brk(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_ast_node *parent, *region, *depart, *list;

	list = ctx->current_node;
	parent = of_ir_node_get_parent(list);

	/* Get the region node of last loop. */
	region = node_stack_top(ctx->loop_stack);

	/* Insert depart node to jump to exit point of loop region. */
	depart = of_ir_node_depart(ctx->shader, region);
	of_ir_node_insert(parent, depart);
	of_ir_node_insert(depart, list);

	/*
	 * Create instruction list node for unreachable instructions.
	 *
	 * NOTE: This is needed for uniform handling of insertion of other
	 * nodes. Instructions beyond this point should not happen, and if so,
	 * they will be removed by dead code elimination.
	 */
	list = of_ir_node_list(ctx->shader);
	of_ir_node_insert(parent, list);
	ctx->current_node = list;
}

static void
translate_endloop(struct of_compile_context *ctx,
		  struct tgsi_full_instruction *inst, unsigned long data)
{
	struct of_ir_ast_node *parent, *region, *repeat, *list;

	repeat = of_ir_node_get_parent(ctx->current_node);
	region = of_ir_node_get_parent(repeat);
	parent = of_ir_node_get_parent(region);

	/* Drop loop region from the stack. */
	node_stack_pop(ctx->loop_stack);

	/* Create instruction list node. */
	list = of_ir_node_list(ctx->shader);
	of_ir_node_insert(parent, list);
	ctx->current_node = list;
}

/*
 * Helper for TGSI instructions with direct translation.
 */

static void
translate_direct(struct of_compile_context *ctx,
		 struct tgsi_full_instruction *inst, unsigned long data)
{
	const struct of_ir_opc_info *info = of_ir_get_opc_info(data);
	struct of_ir_instr_template instr;
	int src;

	assert(inst->Instruction.NumSrcRegs == info->num_srcs);
	assert(inst->Instruction.NumDstRegs <= 1);

	memset(&instr, 0, sizeof(instr));

	instr.opc = data;
	if (inst->Instruction.NumDstRegs)
		instr.dst.reg = get_dst_reg(ctx, inst);
	for (src = 0; src < info->num_srcs; ++src)
		instr.src[src].reg = get_src_reg(ctx, inst, src);

	of_ir_instr_insert_templ(ctx->shader, ctx->current_node,
					NULL, &instr, 1);
}

/*
 * Main part of compiler/translator:
 */

#define IR_DIRECT(_tgsi, _op)				\
	[_tgsi] = {					\
		.handler = translate_direct,		\
		.handler_data = _op,	\
	}

#define IR_EMULATE(_tgsi, _handler, _data)		\
	[_tgsi] = {					\
		.handler = _handler,			\
		.handler_data = _data,			\
	}

static const struct of_tgsi_map_entry translate_table[] = {
	/* ALU */
	IR_DIRECT(TGSI_OPCODE_MOV, OF_OP_MOV),
	IR_DIRECT(TGSI_OPCODE_RCP, OF_OP_RCP),
	IR_DIRECT(TGSI_OPCODE_RSQ, OF_OP_RSQ),
	IR_DIRECT(TGSI_OPCODE_MUL, OF_OP_MUL),
	IR_DIRECT(TGSI_OPCODE_ADD, OF_OP_ADD),
	IR_EMULATE(TGSI_OPCODE_SUB, translate_sub, 0),
	IR_DIRECT(TGSI_OPCODE_MIN, OF_OP_MIN),
	IR_DIRECT(TGSI_OPCODE_MAX, OF_OP_MAX),
	IR_DIRECT(TGSI_OPCODE_MAD, OF_OP_MAD),
	IR_EMULATE(TGSI_OPCODE_CLAMP, translate_clamp, 0),
	IR_DIRECT(TGSI_OPCODE_FLR, OF_OP_FLR),
	IR_EMULATE(TGSI_OPCODE_ROUND, translate_round, 0),
	IR_EMULATE(TGSI_OPCODE_SSG, translate_ssg, 0),
	IR_DIRECT(TGSI_OPCODE_ARL, OF_OP_MOVA),
	IR_DIRECT(TGSI_OPCODE_EX2, OF_OP_EXP),
	IR_DIRECT(TGSI_OPCODE_LG2, OF_OP_LOG),
	IR_EMULATE(TGSI_OPCODE_ABS, translate_abs, 0),
	IR_EMULATE(TGSI_OPCODE_COS, translate_trig, 0),
	IR_EMULATE(TGSI_OPCODE_SIN, translate_trig, 0),
	IR_DIRECT(TGSI_OPCODE_SLT, OF_OP_SLT),
	IR_DIRECT(TGSI_OPCODE_SGE, OF_OP_SGE),
	IR_EMULATE(TGSI_OPCODE_SNE, translate_sne_seq, 0),
	IR_EMULATE(TGSI_OPCODE_SEQ, translate_sne_seq, 0),
	IR_DIRECT(TGSI_OPCODE_CMP, OF_OP_CMP),
	IR_DIRECT(TGSI_OPCODE_KILL_IF, OF_OP_TEXKILL),
	IR_EMULATE(TGSI_OPCODE_KILL, translate_kill, 0),
	IR_DIRECT(TGSI_OPCODE_DST, OF_OP_DST),
	IR_EMULATE(TGSI_OPCODE_XPD, translate_xpd, 0),
	IR_EMULATE(TGSI_OPCODE_SCS, translate_trig, 0),
	IR_EMULATE(TGSI_OPCODE_LRP, translate_lrp, 0),
	IR_DIRECT(TGSI_OPCODE_FRC, OF_OP_FRC),
	IR_EMULATE(TGSI_OPCODE_POW, translate_pow, 0),
	IR_EMULATE(TGSI_OPCODE_LIT, translate_lit, 0),
	IR_DIRECT(TGSI_OPCODE_DP4, OF_OP_DP4),
	IR_DIRECT(TGSI_OPCODE_DP3, OF_OP_DP3),
	IR_DIRECT(TGSI_OPCODE_DPH, OF_OP_DPH),
	IR_EMULATE(TGSI_OPCODE_DP2, translate_dp2, 0),
	IR_DIRECT(TGSI_OPCODE_DP2A, OF_OP_DP2ADD),
	IR_EMULATE(TGSI_OPCODE_DDX, translate_ddx, 0),
	IR_EMULATE(TGSI_OPCODE_DDY, translate_ddy, 0),
	IR_EMULATE(TGSI_OPCODE_TRUNC, translate_trunc, 0),
	IR_EMULATE(TGSI_OPCODE_CEIL, translate_ceil, 0),
	IR_DIRECT(TGSI_OPCODE_NOP, OF_OP_NOP),
	IR_DIRECT(TGSI_OPCODE_END, OF_OP_RET),

	/* Dynamic flow control */
	IR_EMULATE(TGSI_OPCODE_IF, translate_if, 0),
	IR_EMULATE(TGSI_OPCODE_ELSE, translate_else, 0),
	IR_EMULATE(TGSI_OPCODE_ENDIF, translate_endif, 0),

	/* Subroutines */
	IR_EMULATE(TGSI_OPCODE_BGNSUB, translate_bgnsub, 0),
	IR_DIRECT(TGSI_OPCODE_RET, OF_OP_RET),
	IR_EMULATE(TGSI_OPCODE_ENDSUB, translate_endsub, 0),
	IR_EMULATE(TGSI_OPCODE_CAL, translate_cal, 0),

	/* Loops */
	IR_EMULATE(TGSI_OPCODE_BGNLOOP, translate_bgnloop, 0),
	IR_EMULATE(TGSI_OPCODE_BRK, translate_brk, 0),
	IR_EMULATE(TGSI_OPCODE_ENDLOOP, translate_endloop, 0),

	/* Texture lookup */
	IR_EMULATE(TGSI_OPCODE_TEX, translate_tex, 0),
	IR_EMULATE(TGSI_OPCODE_TXP, translate_tex, 0),
};

static void
translate_instruction(struct of_compile_context *ctx)
{
	struct tgsi_full_instruction *inst =
					&ctx->parser.FullToken.FullInstruction;
	unsigned opc = inst->Instruction.Opcode;

	if (opc == TGSI_OPCODE_END)
		return;

	if (opc >= ARRAY_SIZE(translate_table)
	    || !translate_table[opc].handler)
		compile_error(ctx, "unknown TGSI opc: %s\n",
				tgsi_get_opcode_name(opc));

	translate_table[opc].handler(ctx, inst,
					translate_table[opc].handler_data);
}

static const token_handler_t compile_token_handlers[] = {
	[TGSI_TOKEN_TYPE_INSTRUCTION] = translate_instruction,
};

/*
 * Initialization.
 */

static void
init_handle_declaration(struct of_compile_context *ctx)
{
	struct tgsi_full_declaration *decl;
	unsigned first;
	unsigned last;
	unsigned file;
	unsigned name;

	decl = &ctx->parser.FullToken.FullDeclaration;
	first = decl->Range.First;
	last = decl->Range.Last;
	file = decl->Declaration.File;

	ctx->num_regs[file] = MAX2(ctx->num_regs[file], last + 1);

	switch (file) {
	case TGSI_FILE_OUTPUT:
		assert(decl->Declaration.Semantic);  // TODO is this ever not true?
		name = decl->Semantic.Name;

		ctx->output_map[first] = decl->Semantic;

		if (ctx->type == TGSI_PROCESSOR_VERTEX) {
			switch (name) {
			case TGSI_SEMANTIC_POSITION:
				if (ctx->position == -1UL)
					ctx->position = first;
				break;
			case TGSI_SEMANTIC_PSIZE:
				if (ctx->psize == -1UL)
					ctx->psize = first;
				break;
			case TGSI_SEMANTIC_BCOLOR:
			case TGSI_SEMANTIC_COLOR:
			case TGSI_SEMANTIC_FOG:
			case TGSI_SEMANTIC_GENERIC:
				break;
			default:
				compile_error(ctx, "unsupported VS output semantic: %s\n",
					tgsi_semantic_names[name]);
			}
		} else {
			switch (name) {
			case TGSI_SEMANTIC_COLOR:
				break;
			default:
				compile_error(ctx, "unsupported FS output semantic: %s\n",
					tgsi_semantic_names[name]);
			}
		}
		break;

	case TGSI_FILE_INPUT:
		ctx->input_map[first] = decl->Semantic;
		break;
	}
}

static void
init_handle_immediate(struct of_compile_context *ctx)
{
	struct tgsi_full_immediate *imm = &ctx->parser.FullToken.FullImmediate;

	compile_assert(ctx, imm->Immediate.DataType == TGSI_IMM_FLOAT32);
	memcpy(&ctx->immediates[ctx->num_immediates], imm->u, 16);
	ctx->num_immediates += 4;
}

static const token_handler_t init_token_handlers[] = {
	[TGSI_TOKEN_TYPE_DECLARATION] = init_handle_declaration,
	[TGSI_TOKEN_TYPE_IMMEDIATE] = init_handle_immediate,
};

static struct of_compile_context *
compile_init(const struct tgsi_token *tokens)
{
	struct of_compile_context *ctx;
	struct of_ir_ast_node *region, *list;
	unsigned ret;

	ctx = CALLOC_STRUCT(of_compile_context);
	if (!ctx) {
		DBG("failed to allocate compile context");
		return ctx;
	}

	switch (ctx->parser.FullHeader.Processor.Processor) {
	case TGSI_PROCESSOR_VERTEX:
		ctx->type = OF_SHADER_VERTEX;
		break;
	case TGSI_PROCESSOR_FRAGMENT:
		ctx->type = OF_SHADER_PIXEL;
		break;
	default:
		assert(0);
	}

	ctx->shader = of_ir_shader_create(ctx->type);
	if (!ctx->shader) {
		DBG("failed to create IR shader");
		goto fail;
	}

	ctx->loop_stack = of_stack_create(sizeof(struct of_ir_ast_node *), 4);
	if (!ctx->loop_stack)
		goto fail;

	ctx->subroutine_ht = util_hash_table_create(integer_hash,
							pointer_compare);
	if (!ctx->subroutine_ht) {
		DBG("failed to create subroutine hash table");
		goto fail;
	}

	region = of_ir_node_region(ctx->shader);
	list = of_ir_node_list(ctx->shader);
	of_ir_node_insert(region, list);
	ctx->current_node = list;

	ret = tgsi_parse_init(&ctx->parser, tokens);
	if (ret != TGSI_PARSE_OK) {
		DBG("failed to init TGSI parser (%u)", ret);
		goto fail;
	}

	ctx->type = ctx->parser.FullHeader.Processor.Processor;
	ctx->position = -1U;
	ctx->psize = -1U;
	ctx->tokens = tokens;

	/* do first pass to extract declarations: */
	process_tokens(ctx, init_token_handlers,
			ARRAY_SIZE(init_token_handlers));

	tgsi_parse_free(&ctx->parser);

	ret = tgsi_parse_init(&ctx->parser, tokens);
	if (ret != TGSI_PARSE_OK) {
		DBG("failed to init TGSI parser second time (%u)", ret);
		goto fail;
	}

	return ctx;

fail:
	if (ctx->subroutine_ht)
		util_hash_table_destroy(ctx->subroutine_ht);
	if (ctx->loop_stack)
		of_stack_destroy(ctx->loop_stack);
	if (ctx->shader)
		of_ir_shader_destroy(ctx->shader);
	FREE(ctx);

	return NULL;
}

static void
compile_free(struct of_compile_context *ctx)
{
	of_stack_destroy(ctx->loop_stack);
	tgsi_parse_free(&ctx->parser);
	FREE(ctx);
}

/*
 * Compiler entry point.
 */

int
of_compile_shader(struct of_shader_stateobj *so)
{
	struct of_compile_context *ctx;

	of_ir_shader_destroy(so->ir);
	so->ir = NULL;
	FREE(so->immediates);
	so->immediates = NULL;
	so->num_immediates = 0;

	ctx = compile_init(so->tokens);
	if (!ctx)
		return -1;

	process_tokens(ctx, compile_token_handlers,
			ARRAY_SIZE(compile_token_handlers));

	so->ir = ctx->shader;
	ctx->num_immediates = ROUND_UP(ctx->num_immediates, 4);
	so->immediates = MALLOC(ctx->num_immediates * sizeof(uint32_t));
	so->num_immediates = ctx->num_immediates;
	so->first_immediate = ctx->num_regs[TGSI_FILE_CONSTANT];
	memcpy(so->immediates, ctx->immediates,
		ctx->num_immediates * sizeof(uint32_t));

	so->num_inputs = ctx->num_regs[TGSI_FILE_INPUT];

	compile_free(ctx);
	return 0;
}
