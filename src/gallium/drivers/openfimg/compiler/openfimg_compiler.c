/*
 * Copyright (C) 2013-2014 Tomasz Figa <tomasz.figa@gmail.com>
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

#include "pipe/p_state.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
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

#define TMP_REG_BAND_SIZE	16
#define OF_SRC_REG_NUM		3

enum of_register_band {
	REG_BAND_EVEN,
	REG_BAND_ODD,

	REG_BAND_NUM,
	REG_BAND_ANY = REG_BAND_NUM,
};

struct of_compile_context {
	struct of_program_stateobj *prog;
	struct of_shader_stateobj *so;

	struct tgsi_parse_context parser;
	unsigned type;

	/* Internal-Temporary and Predicate register assignment:
	 *
	 * Some TGSI instructions which translate into multiple actual
	 * instructions need one or more temporary registers, which are not
	 * assigned from TGSI perspective (ie. not TGSI_FILE_TEMPORARY).
	 * And some instructions (texture fetch) cannot write directly to
	 * output registers.  We could be more clever and re-use dst or a
	 * src register in some cases.  But for now don't try to be clever.
	 * Eventually we should implement an optimization pass that re-
	 * juggles the register usage and gets rid of unneeded temporaries.
	 *
	 * The predicate register must be valid across multiple TGSI
	 * instructions, but internal temporary's do not.  For this reason,
	 * once the predicate register is requested, until it is no longer
	 * needed, it gets the first register slot after after the TGSI
	 * assigned temporaries (ie. num_regs[TGSI_FILE_TEMPORARY]), and the
	 * internal temporaries get the register slots above this.
	 */

	int pred_reg;
	int num_internal_temps;

	uint8_t num_regs[TGSI_FILE_COUNT];
	uint32_t temp_bitmap[REG_BAND_NUM];
	unsigned temp_count[REG_BAND_NUM];

	/* maps input register idx to prog->export_linkage idx: */
	uint8_t input_export_idx[64];

	/* maps output register idx to prog->export_linkage idx: */
	uint8_t output_export_idx[64];

	/* idx/slot for last compiler generated immediate */
	unsigned immediate_idx;

	// TODO we can skip emit exports in the VS that the FS doesn't need..
	// and get rid perhaps of num_param..
	unsigned num_position, num_param;
	unsigned position, psize;

	/* current shader */
	struct of_ir_shader *shader;
};

typedef void (*of_tgsi_opcode_handler_t)(struct of_compile_context *,
					 struct tgsi_full_instruction *,
					 unsigned long);

struct of_tgsi_map_entry {
	of_tgsi_opcode_handler_t handler;
	unsigned long handler_data;
};

static int
semantic_idx(struct tgsi_declaration_semantic *semantic)
{
	int idx = semantic->Name;
	if (idx == TGSI_SEMANTIC_GENERIC)
		idx = TGSI_SEMANTIC_COUNT + semantic->Index;
	return idx;
}

typedef void (*token_handler_t)(struct of_compile_context *ctx);

static void
process_tokens(struct of_compile_context *ctx, const token_handler_t *handlers,
	       unsigned num_handlers)
{
	while (!tgsi_parse_end_of_tokens(&ctx->parser)) {
		token_handler_t handler;
		unsigned token_type;

		tgsi_parse_token(&ctx->parser);

		token_type = ctx->parser.FullToken.Token.Type;

		if (token_type > num_handlers)
			continue;

		handler = handlers[token_type];
		if (!handler)
			continue;

		handler(ctx);
	}
}

static void
init_handle_declaration(struct of_compile_context *ctx)
{
	struct tgsi_full_declaration *decl =
					&ctx->parser.FullToken.FullDeclaration;

	switch (decl->Declaration.File) {
	case TGSI_FILE_OUTPUT: {
		unsigned name = decl->Semantic.Name;

		assert(decl->Declaration.Semantic);  // TODO is this ever not true?

		ctx->output_export_idx[decl->Range.First] =
				semantic_idx(&decl->Semantic);

		if (ctx->type == TGSI_PROCESSOR_VERTEX) {
			switch (name) {
			case TGSI_SEMANTIC_POSITION:
				ctx->position = ctx->num_regs[TGSI_FILE_OUTPUT];
				ctx->num_position++;
				break;
			case TGSI_SEMANTIC_PSIZE:
				ctx->psize = ctx->num_regs[TGSI_FILE_OUTPUT];
				ctx->num_position++;
				break;
			case TGSI_SEMANTIC_COLOR:
			case TGSI_SEMANTIC_GENERIC:
				ctx->num_param++;
				break;
			default:
				DBG("unknown VS semantic name: %s",
						tgsi_semantic_names[name]);
				assert(0);
			}
		} else {
			switch (name) {
			case TGSI_SEMANTIC_COLOR:
			case TGSI_SEMANTIC_GENERIC:
				ctx->num_param++;
				break;
			default:
				DBG("unknown PS semantic name: %s",
						tgsi_semantic_names[name]);
				assert(0);
			}
		}

		break; }

	case TGSI_FILE_INPUT:
		ctx->input_export_idx[decl->Range.First] =
				semantic_idx(&decl->Semantic);
		break;

	case TGSI_FILE_TEMPORARY: {
		unsigned tmp;

		for (tmp = decl->Range.First; tmp <= decl->Range.Last; ++tmp) {
			unsigned band = tmp % 2;
			unsigned reg = tmp / 2;

			if (ctx->temp_bitmap[band] & (1 << reg)) {
				ctx->temp_bitmap[band] &= ~(1 << reg);
				++ctx->temp_count[band];
			}
		}

		break; }
	}

	ctx->num_regs[decl->Declaration.File] =
		MAX2(ctx->num_regs[decl->Declaration.File],
		decl->Range.Last + 1);
}

static void
init_handle_immediate(struct of_compile_context *ctx)
{
	struct tgsi_full_immediate *imm = &ctx->parser.FullToken.FullImmediate;
	unsigned n = ctx->so->num_immediates++;

	memcpy(ctx->so->immediates[n].val, imm->u, 16);
}

static const token_handler_t init_token_handlers[] = {
	[TGSI_TOKEN_TYPE_DECLARATION] = init_handle_declaration,
	[TGSI_TOKEN_TYPE_IMMEDIATE] = init_handle_immediate,
};

static unsigned
compile_init(struct of_compile_context *ctx, struct of_shader_stateobj *so)
{
	unsigned ret;

	ctx->so = so;
	ctx->shader = so->ir;

	ret = tgsi_parse_init(&ctx->parser, so->tokens);
	if (ret != TGSI_PARSE_OK)
		return ret;

	ctx->type = ctx->parser.FullHeader.Processor.Processor;
	ctx->position = ~0;
	ctx->psize = ~0;
	ctx->num_position = 0;
	ctx->num_param = 0;
	ctx->immediate_idx = 0;
	ctx->pred_reg = -1;
	ctx->num_internal_temps = 0;

	memset(ctx->num_regs, 0, sizeof(ctx->num_regs));
	memset(ctx->input_export_idx, 0, sizeof(ctx->input_export_idx));
	memset(ctx->output_export_idx, 0, sizeof(ctx->output_export_idx));
	memset(ctx->temp_bitmap, 0xff, sizeof(ctx->temp_bitmap));

	/* do first pass to extract declarations: */
	process_tokens(ctx, init_token_handlers,
			ARRAY_SIZE(init_token_handlers));

	/* TGSI generated immediates are always entire vec4's, ones we
	 * generate internally are not:
	 */
	ctx->immediate_idx = ctx->so->num_immediates * 4;

	ctx->so->first_immediate = ctx->num_regs[TGSI_FILE_CONSTANT];

	tgsi_parse_free(&ctx->parser);

	return tgsi_parse_init(&ctx->parser, so->tokens);
}

static void
compile_free(struct of_compile_context *ctx)
{
	tgsi_parse_free(&ctx->parser);
}

/*
 * For vertex shaders (VS):
 * --- ------ -------------
 *
 *   Inputs:     R1-R(num_input)
 *   Constants:  C0-C(num_const-1)
 *   Immediates: C(num_const)-C(num_const+num_imm-1)
 *   Outputs:    export0-export(n) and export62, export63
 *      n is # of outputs minus gl_Position (export62) and gl_PointSize (export63)
 *   Temps:      R(num_input+1)-R(num_input+num_temps)
 *
 * R0 could be clobbered after the vertex fetch instructions.. so we
 * could use it for one of the temporaries.
 *
 * TODO: maybe the vertex fetch part could fetch first input into R0 as
 * the last vtx fetch instruction, which would let us use the same
 * register layout in either case.. although this is not what the blob
 * compiler does.
 *
 *
 * For frag shaders (PS):
 * --- ---- -------------
 *
 *   Inputs:     R0-R(num_input-1)
 *   Constants:  same as VS
 *   Immediates: same as VS
 *   Outputs:    export0-export(num_outputs)
 *   Temps:      R(num_input)-R(num_input+num_temps-1)
 *
 * In either case, immediates are are postpended to the constants
 * (uniforms).
 *
 */

static struct of_ir_register *
get_dst_reg(struct of_compile_context *ctx, struct tgsi_full_instruction *inst,
	    const struct tgsi_dst_register *dst)
{
	enum of_ir_reg_type type;
	unsigned flags = 0;
	unsigned num;
	char swiz[5];

	switch (dst->File) {
	case TGSI_FILE_OUTPUT:
		type = OF_IR_REG_O;
		num = dst->Index;

		if (ctx->type == TGSI_PROCESSOR_FRAGMENT)
			break;

		/*
		 * Position must always be the first output of vertex shader,
		 * so, if there is any other element there, we swap it with
		 * position.
		 */
		if (dst->Index == ctx->position)
			num = 0;
		else if (dst->Index == 0)
			num = ctx->position;

		break;

	case TGSI_FILE_TEMPORARY:
		type = OF_IR_REG_R;
		num = dst->Index;
		break;

	default:
		DBG("unsupported dst register file: %s",
			tgsi_file_name(dst->File));
		assert(0);
		return NULL;
	}

	switch (inst->Instruction.Saturate) {
	case TGSI_SAT_NONE:
		break;
	case TGSI_SAT_ZERO_ONE:
		flags |= OF_IR_REG_SAT;
		break;
	case TGSI_SAT_MINUS_PLUS_ONE:
		DBG("unsupported saturate");
		assert(0);
		return NULL;
	}

	swiz[0] = (dst->WriteMask & TGSI_WRITEMASK_X) ? 'x' : '_';
	swiz[1] = (dst->WriteMask & TGSI_WRITEMASK_Y) ? 'y' : '_';
	swiz[2] = (dst->WriteMask & TGSI_WRITEMASK_Z) ? 'z' : '_';
	swiz[3] = (dst->WriteMask & TGSI_WRITEMASK_W) ? 'w' : '_';
	swiz[4] = '\0';

	return of_ir_reg_create(ctx->shader, type, num, swiz, flags);
}

static struct of_ir_register *
get_src_reg(struct of_compile_context *ctx, const struct tgsi_src_register *src)
{
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
		type = OF_IR_REG_C;
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
		DBG("unsupported src register file: %s",
			tgsi_file_name(src->File));
		assert(0);
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

/*
 * Helpers for TGSI instructions that don't map to a single shader instr:
 */

/* POW(a,b) = EXP2(b * LOG2(a)) */
static void
translate_pow(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_tex(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	unsigned opc = inst->Instruction.Opcode;
	struct of_ir_instruction *instr;
	struct of_ir_register *coord = NULL;
	struct of_ir_register *reg;

	if (opc == TGSI_OPCODE_TXP) {
		static const char *swiz[] = {
			[TGSI_SWIZZLE_X] = "xxxx",
			[TGSI_SWIZZLE_Y] = "yyyy",
			[TGSI_SWIZZLE_Z] = "zzzz",
			[TGSI_SWIZZLE_W] = "wwww",
		};

		/* TXP - Projective Texture Lookup:
		 *
		 *  coord.x = src0.x / src.w
		 *  coord.y = src0.y / src.w
		 *  coord.z = src0.z / src.w
		 *  coord.w = src0.w
		 *  bias = 0.0
		 *
		 *  dst = texture_sample(unit, coord, bias)
		 */

		/* reg.x___ = 1.0 / src0.wwww */
		instr = of_ir_instr_create(ctx->shader, OF_OP_RCP);

		reg = of_ir_reg_temporary(ctx->shader);
		of_ir_reg_set_swizzle(reg, "x___");
		of_ir_instr_add_dst(instr, reg);

		reg = of_ir_reg_clone(ctx->shader, reg);
		of_ir_reg_set_swizzle(reg,
					swiz[inst->Src[0].Register.SwizzleW]);
		of_ir_instr_add_src(instr, reg);

		of_ir_instr_insert(ctx->shader, NULL, NULL, instr);

		/* reg.xyz_ = src0.xyzw * src0.xxxx */
		instr = of_ir_instr_create(ctx->shader, OF_OP_MUL);

		reg = of_ir_reg_clone(ctx->shader, reg);
		of_ir_reg_set_swizzle(reg, "xyz_");
		of_ir_instr_add_dst(instr, reg);

		reg = of_ir_reg_clone(ctx->shader, reg);
		of_ir_reg_set_swizzle(reg, "xxxx");
		of_ir_instr_add_src(instr, reg);

		reg = get_src_reg(ctx, &inst->Src[0].Register);
		of_ir_instr_add_src(instr, reg);

		of_ir_instr_insert(ctx->shader, NULL, NULL, instr);

		coord = of_ir_reg_clone(ctx->shader, reg);
		of_ir_reg_set_swizzle(coord, "xyzw");
	}

	assert(inst->Texture.NumOffsets <= 1); // TODO what to do in other cases?

	instr = of_ir_instr_create(ctx->shader, OF_OP_TEXLD);

	reg = get_dst_reg(ctx, inst, &inst->Dst[0].Register);
	of_ir_instr_add_dst(instr, reg);

	reg = get_src_reg(ctx, &inst->Src[1].Register);
	of_ir_instr_add_src(instr, reg);

	if (!coord)
		coord = get_src_reg(ctx, &inst->Src[0].Register);
	of_ir_instr_add_src(instr, coord);

	of_ir_instr_insert(ctx->shader, NULL, NULL, instr);
}

/* LRP(a,b,c) = (a * b) + ((1 - a) * c) */
static void
translate_lrp(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_trig(struct of_compile_context *ctx,
	       struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_lit(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_sub(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_cnd(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_clamp(struct of_compile_context *ctx,
		struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_round(struct of_compile_context *ctx,
		struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_xpd(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_abs(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_rcc(struct of_compile_context *ctx,
	      struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_if(struct of_compile_context *ctx,
	     struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_else(struct of_compile_context *ctx,
	       struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_endif(struct of_compile_context *ctx,
		struct tgsi_full_instruction *inst, unsigned long data)
{
	DBG("TODO");
}

static void
translate_direct(struct of_compile_context *ctx,
		 struct tgsi_full_instruction *inst, unsigned long data)
{
	const struct of_ir_opc_info *info = of_ir_get_opc_info(data);
	struct of_ir_instruction *ins;
	struct of_ir_register *reg;
	int src;

	assert(inst->Instruction.NumSrcRegs == info->num_srcs);
	assert(inst->Instruction.NumDstRegs == 1);

	ins = of_ir_instr_create(ctx->shader, data);

	reg = get_dst_reg(ctx, inst, &inst->Dst[0].Register);
	of_ir_instr_add_dst(ins, reg);

	for (src = 0; src < info->num_srcs; ++src) {
		reg = get_src_reg(ctx, &inst->Src[src].Register);
		of_ir_instr_add_src(ins, reg);
	}

	of_ir_instr_insert(ctx->shader, NULL, NULL, ins);
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
	IR_DIRECT(TGSI_OPCODE_ARL, OF_OP_FLR),
	IR_DIRECT(TGSI_OPCODE_MOV, OF_OP_MOV),
	IR_EMULATE(TGSI_OPCODE_LIT, translate_lit, 0),
	IR_DIRECT(TGSI_OPCODE_RCP, OF_OP_RCP),
	IR_DIRECT(TGSI_OPCODE_RSQ, OF_OP_RSQ),
	IR_DIRECT(TGSI_OPCODE_EXP, OF_OP_EXP_LIT),
	IR_DIRECT(TGSI_OPCODE_LOG, OF_OP_LOG_LIT),
	IR_DIRECT(TGSI_OPCODE_MUL, OF_OP_MUL),
	IR_DIRECT(TGSI_OPCODE_ADD, OF_OP_ADD),
	IR_DIRECT(TGSI_OPCODE_DP3, OF_OP_DP3),
	IR_DIRECT(TGSI_OPCODE_DP4, OF_OP_DP4),
	IR_DIRECT(TGSI_OPCODE_DST, OF_OP_DST),
	IR_DIRECT(TGSI_OPCODE_MIN, OF_OP_MIN),
	IR_DIRECT(TGSI_OPCODE_MAX, OF_OP_MAX),
	IR_DIRECT(TGSI_OPCODE_SLT, OF_OP_SLT),
	IR_DIRECT(TGSI_OPCODE_SGE, OF_OP_SGE),
	IR_DIRECT(TGSI_OPCODE_MAD, OF_OP_MAD),
	IR_EMULATE(TGSI_OPCODE_SUB, translate_sub, 0),
	IR_EMULATE(TGSI_OPCODE_LRP, translate_lrp, 0),
	IR_EMULATE(TGSI_OPCODE_CND, translate_cnd, 0),
	IR_DIRECT(TGSI_OPCODE_DP2A, OF_OP_DP2ADD),
	IR_DIRECT(TGSI_OPCODE_FRC, OF_OP_FRC),
	IR_EMULATE(TGSI_OPCODE_CLAMP, translate_clamp, 0),
	IR_DIRECT(TGSI_OPCODE_FLR, OF_OP_FLR),
	IR_EMULATE(TGSI_OPCODE_ROUND, translate_round, 0),
	IR_DIRECT(TGSI_OPCODE_EX2, OF_OP_EXP),
	IR_DIRECT(TGSI_OPCODE_LG2, OF_OP_LOG),
	IR_EMULATE(TGSI_OPCODE_POW, translate_pow, 0),
	IR_EMULATE(TGSI_OPCODE_XPD, translate_xpd, 0),
	IR_EMULATE(TGSI_OPCODE_ABS, translate_abs, 0),
	IR_EMULATE(TGSI_OPCODE_RCC, translate_rcc, 0),
	IR_DIRECT(TGSI_OPCODE_DPH, OF_OP_DPH),
	IR_EMULATE(TGSI_OPCODE_COS, translate_trig, 0),
	IR_EMULATE(TGSI_OPCODE_SIN, translate_trig, 0),
	IR_EMULATE(TGSI_OPCODE_TEX, translate_tex, 0),
	IR_EMULATE(TGSI_OPCODE_TXP, translate_tex, 0),
	IR_DIRECT(TGSI_OPCODE_CMP, OF_OP_CMP),
	IR_EMULATE(TGSI_OPCODE_IF, translate_if, 0),
	IR_EMULATE(TGSI_OPCODE_ELSE, translate_else, 0),
	IR_EMULATE(TGSI_OPCODE_ENDIF, translate_endif, 0),
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
	    || !translate_table[opc].handler) {
		DBG("unknown TGSI opc: %s", tgsi_get_opcode_name(opc));
		tgsi_dump(ctx->so->tokens, 0);
		assert(0);
		return;
	}

	translate_table[opc].handler(ctx, inst,
					translate_table[opc].handler_data);
}

static const token_handler_t compile_token_handlers[] = {
	[TGSI_TOKEN_TYPE_INSTRUCTION] = translate_instruction,
};

int
of_compile_shader(struct of_shader_stateobj *so)
{
	struct of_compile_context ctx;

	of_ir_shader_destroy(so->ir);
	so->ir = of_ir_shader_create();
	so->num_immediates = 0;

	if (compile_init(&ctx, so) != TGSI_PARSE_OK)
		return -1;

	process_tokens(&ctx, compile_token_handlers,
			ARRAY_SIZE(compile_token_handlers));

	compile_free(&ctx);

	if (ctx.temp_count[REG_BAND_EVEN] > TMP_REG_BAND_SIZE
	    || ctx.temp_count[REG_BAND_ODD] > TMP_REG_BAND_SIZE) {
		DBG("too many temporaries used (%u even and %u odd)",
			ctx.temp_count[REG_BAND_EVEN],
			ctx.temp_count[REG_BAND_ODD]);
		return -1;
	}

	return 0;
}
