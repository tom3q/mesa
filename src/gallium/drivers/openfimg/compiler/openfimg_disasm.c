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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>

#include "openfimg_disasm.h"
#include "openfimg_util.h"

#include "fimg_3dse.xml.h"

struct __attribute__((__packed__)) instr {
	struct {
		unsigned src2_regnum	:5;
		unsigned		:3;
		unsigned src2_regtype	:3;
		unsigned src2_ar	:1;
		unsigned		:2;
		unsigned src2_negate	:1;
		unsigned src2_abs	:1;
		unsigned src2_swizzle	:8;
		unsigned src1_regnum	:5;
		unsigned pred_channel	:2;
		unsigned pred_unknown	:1;
	};
	struct {
		unsigned src1_regtype	:3;
		unsigned src1_ar	:1;
		unsigned pred_negate	:1;
		unsigned pred_enable	:1;
		unsigned src1_negate	:1;
		unsigned src1_abs	:1;
		unsigned src1_swizzle	:8;
		unsigned src0_regnum	:8;
		unsigned src0_regtype	:3;
		unsigned src0_ar_chan	:2;
		unsigned src0_ar	:1;
		unsigned src0_negate	:1;
		unsigned src0_abs	:1;
	};
	union {
		struct {
			unsigned src0_swizzle	:8;
			unsigned dest_regnum	:5;
			unsigned dest_regtype	:3;
			unsigned dest_a		:1;
			unsigned dest_clamp	:1;
			unsigned		:1;
			unsigned dest_mask	:4;
			unsigned opcode		:6;
			unsigned next_3src	:1;
			unsigned		:2;
		};
		struct {
			unsigned 		:8;
			unsigned branch_offs	:8;
			unsigned branch_dir	:1;
			unsigned		:15;
		};
	};
	struct {
		uint32_t reserved;
	};
};

static const char *levels[] = {
		"\t",
		"\t\t",
		"\t\t\t",
		"\t\t\t\t",
		"\t\t\t\t\t",
		"\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t\t\t",
		"x",
		"x",
		"x",
		"x",
		"x",
		"x",
};

static enum debug_t debug;

/*
 * ALU instructions:
 */

static const char chan_names[] = {
	'x', 'y', 'z', 'w',
};

static const char *src_type_str[] = {
	[OF_SRC_V] = "V",
	[OF_SRC_R] = "R",
	[OF_SRC_C] = "C",
	[OF_SRC_I] = "I",
	[OF_SRC_AL] = "AL",
	[OF_SRC_B] = "B",
	[OF_SRC_P] = "P",
	[OF_SRC_S] = "S",
	[OF_SRC_D] = "D",
	[OF_SRC_VFACE] = "VFACE",
	[OF_SRC_VPOS] = "VPOS",
};

static void print_srcreg(uint32_t num, uint32_t type,
		uint32_t swiz, uint32_t negate, uint32_t abs)
{
	if (negate)
		_debug_printf("-");
	if (abs)
		_debug_printf("|");

	if (type >= ARRAY_SIZE(src_type_str))
		_debug_printf("?%u", num);
	else
		_debug_printf("%s%u", src_type_str[type], num);

	if (swiz != 0xe4) {
		int i;
		_debug_printf(".");
		for (i = 0; i < 4; i++) {
			_debug_printf("%c", chan_names[swiz & 0x3]);
			swiz >>= 2;
		}
	}
	if (abs)
		_debug_printf("|");
}

static const char *dst_type_str[] = {
	[OF_DST_O] = "O",
	[OF_DST_R] = "R",
	[OF_DST_P] = "P",
	[OF_DST_A0] = "A",
	[OF_DST_AL] = "AL",
};

static void print_dstreg(uint32_t num, uint32_t mask, uint32_t type)
{
	if (type >= ARRAY_SIZE(dst_type_str))
		_debug_printf("?%u", num);
	else
		_debug_printf("%s%u", dst_type_str[type], num);

	if (mask != 0xf) {
		int i;
		_debug_printf(".");
		for (i = 0; i < 4; i++) {
			_debug_printf("%c", (mask & 0x1) ? chan_names[i] : '_');
			mask >>= 1;
		}
	}
}

// static void print_export_comment(uint32_t num, enum shader_t type)
// {
// 	const char *name = NULL;
// 	switch (type) {
// 	case SHADER_VERTEX:
// 		switch (num) {
// 		case 62: name = "gl_Position";  break;
// 		case 63: name = "gl_PointSize"; break;
// 		}
// 		break;
// 	case SHADER_FRAGMENT:
// 		switch (num) {
// 		case 0:  name = "gl_FragColor"; break;
// 		}
// 		break;
// 	default:
// 		f_debug_printf(stderr, "unsupported shader type: %u\n", type);
// 		assert(0);
// 		break;
// 	}
// 	/* if we had a symbol table here, we could look
// 	 * up the name of the varying..
// 	 */
// 	if (name) {
// 		_debug_printf("\t; %s", name);
// 	}
// }

enum opcode_type {
	OP_TYPE_FLOW,
	OP_TYPE_NORMAL,
};

struct opcode_disasm {
	enum of_instr_opcode opcode;
	enum opcode_type type;
	unsigned src_count;
	const char *name;
};

#define OP_FLOW(_op, _srcs)		\
	[OF_OP_ ##_op] = {			\
		.opcode = OF_OP_ ##_op,		\
		.type = OP_TYPE_FLOW,	\
		.src_count = _srcs,	\
		.name = #_op,		\
	}

#define OP_NORMAL(_op, _srcs)		\
	[OF_OP_ ##_op] = {			\
		.opcode = OF_OP_ ##_op,		\
		.type = OP_TYPE_NORMAL,	\
		.src_count = _srcs,	\
		.name = #_op,		\
	}

static const struct opcode_disasm opcode_info[] = {
	OP_NORMAL(NOP, 0),
	OP_NORMAL(MOV, 1),
	OP_NORMAL(MOVA, 1),
	OP_NORMAL(MOVC, 2),
	OP_NORMAL(ADD, 2),
	OP_NORMAL(MUL, 2),
	OP_NORMAL(MUL_LIT, 2),
	OP_NORMAL(DP3, 2),
	OP_NORMAL(DP4, 2),
	OP_NORMAL(DPH, 2),
	OP_NORMAL(DST, 2),
	OP_NORMAL(EXP, 1),
	OP_NORMAL(EXP_LIT, 1),
	OP_NORMAL(LOG, 1),
	OP_NORMAL(LOG_LIT, 1),
	OP_NORMAL(RCP, 1),
	OP_NORMAL(RSQ, 1),
	OP_NORMAL(DP2ADD, 3),
	OP_NORMAL(MAX, 2),
	OP_NORMAL(MIN, 2),
	OP_NORMAL(SGE, 2),
	OP_NORMAL(SLT, 2),
	OP_NORMAL(SETP_EQ, 2),
	OP_NORMAL(SETP_GE, 2),
	OP_NORMAL(SETP_GT, 2),
	OP_NORMAL(SETP_NE, 2),
	OP_NORMAL(CMP, 3),
	OP_NORMAL(MAD, 3),
	OP_NORMAL(FRC, 1),
	OP_NORMAL(TEXLD, 2),
	OP_NORMAL(CUBEDIR, 1),
	OP_NORMAL(MAXCOMP, 1),
	OP_NORMAL(TEXLDC, 3),
	OP_NORMAL(TEXKILL, 1),
	OP_NORMAL(MOVIPS, 1),
	OP_NORMAL(ADDI, 2),
	OP_FLOW(B, 0),
	OP_FLOW(BF, 1),
	OP_FLOW(BP, 0),
	OP_FLOW(BFP, 1),
	OP_FLOW(BZP, 1),
	OP_FLOW(CALL, 0),
	OP_FLOW(CALLNZ, 1),
	OP_FLOW(RET, 0),
};

static int disasm_alu(uint32_t *dwords, uint32_t alu_off,
		      int level, enum shader_t type)
{
	struct instr *alu = (struct instr *)dwords;

	_debug_printf("%s", levels[level]);
	if (debug & PRINT_RAW) {
		_debug_printf("%02x: %08x %08x %08x %08x\t", alu_off,
				dwords[0], dwords[1], dwords[2], dwords[3]);
	}

	_debug_printf("%s", opcode_info[alu->opcode].name);

	_debug_printf("\t");

	print_dstreg(alu->dest_regnum, alu->dest_mask, alu->dest_regtype);
	_debug_printf(" = ");
	print_srcreg(alu->src0_regnum, alu->src0_regtype, alu->src0_swizzle,
			alu->src0_negate, alu->src0_abs);
	if (opcode_info[alu->opcode].src_count >= 2) {
		_debug_printf(", ");
		print_srcreg(alu->src1_regnum, alu->src1_regtype,
				alu->src1_swizzle, alu->src1_negate,
				alu->src1_abs);
	}
	if (opcode_info[alu->opcode].src_count >= 3) {
		_debug_printf(", ");
		print_srcreg(alu->src2_regnum, alu->src2_regtype,
				alu->src2_swizzle, alu->src2_negate,
				alu->src2_abs);
	}

	if (alu->dest_clamp)
		_debug_printf(" CLAMP");

	_debug_printf("\n");

	return 0;
}

/*
 * The adreno shader microcode consists of two parts:
 *   1) A CF (control-flow) program, at the header of the compiled shader,
 *      which refers to ALU/FETCH instructions that follow it by address.
 *   2) ALU and FETCH instructions
 */

int disasm_fimg_3dse(struct of_context *ctx, struct pipe_resource *buffer,
		     int sizedwords, int level, enum shader_t type)
{
	struct pipe_transfer *transfer;
	uint32_t *dwords;
	int idx;

	dwords = pipe_buffer_map(&ctx->base, buffer, PIPE_TRANSFER_WRITE,
					&transfer);
	if (!dwords)
		return -1;

	for (idx = 0; idx < sizedwords / 4; idx++)
		disasm_alu(dwords + idx * 4, idx, level, type);

	if (transfer)
		pipe_buffer_unmap(&ctx->base, transfer);

	return 0;
}

void disasm_set_debug(enum debug_t d)
{
	debug = d;
}
