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

#ifndef OF_IR_PRIV_H_
#define OF_IR_PRIV_H_

#include "openfimg_ir.h"

#define DEBUG_MSG(...)  do { if (0) DBG(__VA_ARGS__); } while (0)
#define WARN_MSG(...)   DBG("WARN:  "__VA_ARGS__)
#define ERROR_MSG(...)  DBG("ERROR: "__VA_ARGS__)

/** Representation of single register usage. */
struct of_ir_register {
	/** Register modifiers. */
	enum of_ir_reg_flags flags;
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
	enum of_ir_instr_flags flags;
	/** Number of source registers. */
	unsigned num_srcs;
	/** Source registers. */
	struct of_ir_register *srcs[OF_IR_NUM_SRCS];
	/** Destination register. */
	struct of_ir_register *dst;
	/** Branch target. */
	struct of_ir_cf_block *target;

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
	/** Number of instructions in the block. */
	unsigned num_instrs;

	/** Number of branch targets. */
	unsigned num_targets;
	/** Branch targets. */
	struct {
		/** Basic block which is the target. */
		struct of_ir_cf_block *block;
		/** List head used to link all sources of target block. */
		struct list_head list;
		/** Register that holds test result or NULL if unconditional. */
		struct of_ir_register *condition;
	} targets[OF_IR_NUM_CF_TARGETS];
	struct list_head sources;

	/** Shader to which the basic block belongs. */
	struct of_ir_shader *shader;
	/** List head used to link all basic blocks of the shader. */
	struct list_head list;
	/** List head used by basic block stack. */
	struct list_head cf_stack_list;

	/* Address assigned by assembler. */
	unsigned address;

	/* Various data for processing algorithms */
	unsigned long priv_data;
};

/** Representation of a shader program. */
struct of_ir_shader {
	/** Total number of generated instructions. */
	unsigned num_instrs;
	/** Number of basic blocks. */
	unsigned num_cf_blocks;
	/** List of basic blocks in the program. */
	struct list_head cf_blocks;
	/** Stack of basic blocks */
	struct list_head cf_stack;

	/** Heap to allocate IR data from. */
	uint32_t heap[100 * 4096];
	/** Index of first unused dword on the heap. */
	unsigned heap_idx;

	unsigned num_temporaries;
	const struct of_ir_reg_info *reg_info;
};

struct of_ir_reg_info {
	enum of_instr_src src_type;
	enum of_instr_dst dst_type;

	unsigned num_reads;
	unsigned num_regs;

	bool writable :1;
	bool readable :1;
	bool scalar :1;
	bool al_addr :1;
	bool a0_addr :1;
};

#endif /* OF_IR_PRIV_H_ */
