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

#ifndef OF_PROGRAM_H_
#define OF_PROGRAM_H_

#include "pipe/p_context.h"

#include "openfimg_context.h"

#include "openfimg_ir.h"
#include "openfimg_emit.h"

struct of_shader_stateobj {
	enum of_shader_type type;

	struct pipe_resource *buffer;
	unsigned num_instrs;

	struct tgsi_token *tokens;
	uint32_t hash;

	/* note that we defer compiling shader until we know both vs and ps..
	 * and if one changes, we potentially need to recompile in order to
	 * get varying linkages correct:
	 */
	struct of_ir_shader *ir;

	/* for all shaders, any tex fetch instructions which need to be
	 * patched before assembly:
	 */
	unsigned num_tfetch_instrs;
	struct {
		unsigned samp_id;
		struct of_ir_instruction *instr;
	} tfetch_instrs[64];

	unsigned first_immediate;     /* const reg # of first immediate */
	unsigned num_immediates;
	uint32_t *immediates;

	unsigned num_inputs;
};

/* shader program header as generated by proprietary shader compiler */
struct shader_header {
	uint32_t magic;
	uint32_t version;
	uint32_t header_size;
	uint32_t fimg_version;

	uint32_t instruct_size;
	uint32_t const_float_size;
	uint32_t const_int_size;
	uint32_t const_bool_size;

	uint32_t in_table_size;
	uint32_t out_table_size;
	uint32_t uniform_table_size;
	uint32_t sam_table_size;

	uint32_t reserved[6];
};

void of_program_emit(struct of_context *ctx, struct of_shader_stateobj *so);

void of_prog_init_solid(struct of_context *ctx);
void of_prog_init_blit(struct of_context *ctx);

void of_prog_init(struct pipe_context *pctx);
void of_prog_fini(struct pipe_context *pctx);

#endif /* OF_PROGRAM_H_ */