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

#include "openfimg_context.h"
#include "openfimg_instr.h"
#include "openfimg_util.h"

/* low level intermediate representation of an adreno a2xx shader program */

struct of_ir_shader;

struct of_ir_shader_info {
	uint64_t regs_written;
	uint16_t sizedwords;
	int8_t   max_reg;   /* highest GPR # used by shader */
	uint8_t  max_input_reg;
};

struct of_ir_register {
	enum {
		IR2_REG_NEGATE = 0x4,
		IR2_REG_ABS    = 0x8,
	} flags;
	int num;
	const char *swizzle;
	uint8_t type;
};

enum of_ir_pred {
	IR2_PRED_NONE = 0,
	IR2_PRED_EQ = 1,
	IR2_PRED_NE = 2,
};

enum of_ir_instr_type {
	IR2_CF,
	IR2_ALU,
};

struct of_ir_instruction {
	struct of_ir_shader *shader;
	enum of_ir_instr_type instr_type;
	enum of_ir_pred pred;
	int sync;
	unsigned regs_count;
	struct of_ir_register *regs[5];

	instr_opc_t opc;
	bool clamp :1;
	bool next_3arg :1;
};

struct of_ir_shader {
	unsigned instrs_count;
	struct of_ir_instruction *instrs[512];
	uint32_t heap[100 * 4096];
	unsigned heap_idx;

	enum of_ir_pred pred;  /* pred inherited by newly created instrs */
};

struct of_ir_shader * of_ir_shader_create(void);
void of_ir_shader_destroy(struct of_ir_shader *shader);
struct pipe_resource *of_ir_shader_assemble(struct of_context *ctx,
					  struct of_ir_shader *shader,
					  struct of_ir_shader_info *info);

struct of_ir_instruction * of_ir_instr_create_alu(struct of_ir_shader *shader,
					      instr_opc_t opc);
struct of_ir_instruction * of_ir_instr_create_cf(struct of_ir_shader *shader,
					     instr_opc_t opc);
struct of_ir_register * of_ir_reg_create(struct of_ir_instruction *instr,
				     int num, const char *swizzle, int flags,
				     int type);

#endif /* IR2_H_ */
