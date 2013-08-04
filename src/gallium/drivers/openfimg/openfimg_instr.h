/*
 * Copyright (C) 2013 Tomasz Figa <tomasz.figa@gmail.com>
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

#ifndef INSTR_FIMG_3DSE_H_
#define INSTR_FIMG_3DSE_H_

#define PACKED __attribute__((__packed__))

#include "util/u_math.h"

typedef enum {
	REG_SRC_V = 0,
	REG_SRC_R,
	REG_SRC_C,
	REG_SRC_I,
	REG_SRC_AL,
	REG_SRC_B,
	REG_SRC_P,
	REG_SRC_S,
	REG_SRC_D,
	REG_SRC_VFACE,
	REG_SRC_VPOS
} instr_src_type_t;

typedef enum {
	REG_DST_O = 0,
	REG_DST_R,
	REG_DST_P,
	REG_DST_A0,
	REG_DST_AL
} instr_dst_type_t;

typedef struct PACKED {
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
} instr_t;

typedef enum {
	OP_NOP,
	OP_MOV,
	OP_MOVA,
	OP_MOVC,
	OP_ADD,
	OP_MUL = 0x06,
	OP_MUL_LIT,
	OP_DP3,
	OP_DP4,
	OP_DPH,
	OP_DST,
	OP_EXP,
	OP_EXP_LIT,
	OP_LOG,
	OP_LOG_LIT,
	OP_RCP,
	OP_RSQ,
	OP_DP2ADD,
	OP_MAX = 0x14,
	OP_MIN,
	OP_SGE,
	OP_SLT,
	OP_SETP_EQ,
	OP_SETP_GE,
	OP_SETP_GT,
	OP_SETP_NE,
	OP_CMP,
	OP_MAD,
	OP_FRC,
	OP_FLR,
	OP_TEXLD,
	OP_CUBEDIR,
	OP_MAXCOMP,
	OP_TEXLDC,
	OP_TEXKILL = 0x27,
	OP_MOVIPS,
	OP_ADDI,
	OP_B = 0x30,
	OP_BF,
	OP_BP = 0x34,
	OP_BFP,
	OP_BZP,
	OP_CALL = 0x38,
	OP_CALLNZ,
	OP_RET = 0x3c,
} instr_opc_t;


typedef enum {
	OP_TYPE_FLOW,
	OP_TYPE_NORMAL,
} opcode_type_t;

typedef struct {
	instr_opc_t opcode;
	opcode_type_t type;
	unsigned src_count;
} opcode_info_t;

#endif /* INSTR_H_ */
