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

#ifndef OF_IR_H_
#define OF_IR_H_

#include <stdint.h>
#include <stdbool.h>

#include <util/u_double_list.h>

#include "openfimg_context.h"
#include "openfimg_util.h"

#include "fimg_3dse.xml.h"

/*
 * Low level intermediate representation of a FIMG-3DSE shader program.
 */

struct of_ir_cf_block;

enum {
	OF_IR_NUM_SRCS = 3,
};

enum of_ir_cf_target {
	OF_IR_CF_TARGET_FALL,
	OF_IR_CF_TARGET_JUMP,

	OF_IR_NUM_CF_TARGETS
};

enum of_ir_reg_type {
	/** Temporary register. */
	OF_IR_REG_R,
	/** Shader input register (read-only). */
	OF_IR_REG_V,
	/** Constant float register (read-only). */
	OF_IR_REG_C,
	/** Constant integer register (read-only). */
	OF_IR_REG_I,
	/** Loop count register. */
	OF_IR_REG_AL,
	/** Constant boolean register (read-only). */
	OF_IR_REG_B,
	/** Predicate register. */
	OF_IR_REG_P,
	/** Sampler register (read-only). */
	OF_IR_REG_S,
	/** LOD register (read-only, PS only). */
	OF_IR_REG_D,
	/** Face register (read-only, PS only). */
	OF_IR_REG_VFACE,
	/** Position register (read-only, PS only). */
	OF_IR_REG_VPOS,
	/** Shader output register (write-only). */
	OF_IR_REG_O,
	/** Address register 0 (write-only). */
	OF_IR_REG_A0,

	OF_IR_NUM_REG_TYPES
};

enum of_ir_instr_type {
	OF_IR_CF,
	OF_IR_ALU,
};

struct of_ir_opc_info {
	enum of_ir_instr_type type;
	unsigned num_srcs;
};

struct of_ir_shader_info {
	uint64_t regs_written;
	uint16_t sizedwords;
	int8_t   max_reg;
	uint8_t  max_input_reg;
};

/** Representation of single register usage. */
struct of_ir_register {
	/** Register modifiers. */
	enum {
		OF_IR_REG_NEGATE = (1 << 0),
		OF_IR_REG_ABS = (1 << 1),
		OF_IR_REG_SAT = (1 << 2),
	} flags;
	/* Register number. */
	unsigned num;
	/* Register channel swizzle(map)/mask. */
	char swizzle[4];
	/* Register type. */
	enum of_ir_reg_type type;
};

/** Representation of single instruction. */
struct of_ir_instruction {
	/** Extra modifiers. */
	enum {
		OF_IR_INSTR_NEXT_3SRC = (1 << 0),
	} flags;
	/** Number of source registers. */
	unsigned num_srcs;
	/** Source registers. */
	struct of_ir_register *srcs[OF_IR_NUM_SRCS];
	/** Destination register. */
	struct of_ir_register *dst;

	/** Opcode. */
	enum of_instr_opcode opc;

	/** Basic block to which the instruction belongs. */
	struct of_ir_cf_block *block;
	/** List head to link all instructions of the block. */
	struct list_head list;
};

/** Representation of a basic block (without CF inside). */
struct of_ir_cf_block {
	/** List of PSI() operators at the beginning of the block. */
	struct list_head psis;
	/** List of instructions in the block. */
	struct list_head instrs;

	/** CF instruction that ends the block (if present). */
	struct of_ir_instruction *cf_instr;
	/** Number of branch targets. */
	unsigned num_targets;
	/** Branch targets. */
	struct {
		/** Basic block which is the target. */
		struct of_ir_cf_block *block;
		/** List head used to link all sources of target block. */
		struct list_head list;
	} targets[OF_IR_NUM_CF_TARGETS];

	/** Shader to which the basic block belongs. */
	struct of_ir_shader *shader;
	/** List head used to link all basic blocks of the shader. */
	struct list_head list;

	/* Address assigned by assembler. */
	unsigned address;
};

/** Representation of a shader program. */
struct of_ir_shader {
	/** Total number of generated instructions. */
	unsigned instrs_count;
	/** List of basic blocks in the program. */
	struct list_head cf_blocks;

	/** Heap to allocate IR data from. */
	uint32_t heap[100 * 4096];
	/** Index of first unused dword on the heap. */
	unsigned heap_idx;

	unsigned num_immediates;
	unsigned num_temporaries;
};

extern const struct of_ir_opc_info of_ir_opc_info[];

static inline const struct of_ir_opc_info *
of_ir_get_opc_info(enum of_instr_opcode opc)
{
	return &of_ir_opc_info[opc];
}

struct of_ir_register *of_ir_reg_create(struct of_ir_shader *shader,
					enum of_ir_reg_type type, unsigned num,
					const char *swizzle, unsigned flags);
struct of_ir_register *of_ir_reg_clone(struct of_ir_shader *shader,
				       struct of_ir_register *reg);
struct of_ir_register *of_ir_reg_temporary(struct of_ir_shader *shader);
struct of_ir_register *of_ir_reg_immediate(struct of_ir_shader *shader);
void of_ir_reg_set_swizzle(struct of_ir_register *reg, const char *swizzle);

struct of_ir_instruction *of_ir_instr_create(struct of_ir_shader *shader,
					     enum of_instr_opcode opc);
void of_ir_instr_add_dst(struct of_ir_instruction *instr,
			 struct of_ir_register *reg);
void of_ir_instr_add_src(struct of_ir_instruction *instr,
			 struct of_ir_register *reg);
void of_ir_instr_insert(struct of_ir_shader *shader,
			struct of_ir_cf_block *block,
			struct of_ir_instruction *where,
			struct of_ir_instruction *instr);

struct of_ir_cf_block *of_ir_cf_create(struct of_ir_shader *shader);
void of_ir_cf_insert(struct of_ir_shader *shader, struct of_ir_cf_block *where,
		     struct of_ir_cf_block *block);

struct of_ir_shader *of_ir_shader_create(void);
void of_ir_shader_destroy(struct of_ir_shader *shader);
struct pipe_resource *of_ir_shader_assemble(struct of_context *ctx,
					    struct of_ir_shader *shader,
					    struct of_ir_shader_info *info);

#endif /* OF_IR_H_ */
