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

#ifndef IR2_H_
#define IR2_H_

#include <stdint.h>
#include <stdbool.h>

#include "openfimg_instr.h"

/* low level intermediate representation of an adreno a2xx shader program */

struct ir2_shader;

struct ir2_shader_info {
	uint16_t sizedwords;
	int8_t   max_reg;   /* highest GPR # used by shader */
	uint8_t  max_input_reg;
	uint64_t regs_written;
};

struct ir2_register {
	enum {
		IR2_REG_NEGATE = 0x4,
		IR2_REG_ABS    = 0x8,
	} flags;
	int num;
	char *swizzle;
	uint8_t type;
};

enum ir2_pred {
	IR2_PRED_NONE = 0,
	IR2_PRED_EQ = 1,
	IR2_PRED_NE = 2,
};

struct ir2_instruction {
	struct ir2_shader *shader;
	enum {
		IR2_CF,
		IR2_ALU,
	} instr_type;
	enum ir2_pred pred;
	int sync;
	unsigned regs_count;
	struct ir2_register *regs[5];

	instr_opc_t opc;
	bool clamp :1;
};

struct ir2_shader {
	unsigned instrs_count;
	struct ir2_instruction *instrs[512];
	uint32_t heap[100 * 4096];
	unsigned heap_idx;

	enum ir2_pred pred;  /* pred inherited by newly created instrs */
};

struct ir2_shader * ir2_shader_create(void);
void ir2_shader_destroy(struct ir2_shader *shader);
void * ir2_shader_assemble(struct ir2_shader *shader,
		struct ir2_shader_info *info);

struct ir2_instruction * ir2_instr_create(struct ir2_shader *shader,
					  instr_opc_t opc);

struct ir2_register * ir2_reg_create(struct ir2_instruction *instr,
		int num, const char *swizzle, int flags);

#endif /* IR2_H_ */
