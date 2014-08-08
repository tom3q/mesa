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
#include "openfimg_util.h"

#define DEBUG_MSG(...)  do { if (0) DBG(__VA_ARGS__); } while (0)
#define WARN_MSG(...)   DBG("WARN:  "__VA_ARGS__)
#define ERROR_MSG(...)  DBG("ERROR: "__VA_ARGS__)

typedef void (*dump_ast_callback_t)(struct of_ir_shader *,
				    struct of_ir_ast_node *, unsigned, bool,
				    void *);

struct of_ir_phi {
	struct list_head list;
	uint16_t reg;
	uint16_t dst;
	uint16_t src[];
};

/** Representation of single register usage. */
struct of_ir_register {
	/** Register modifiers. */
	enum of_ir_reg_flags flags;
	/* Register number. */
	uint16_t num;
	/* Variable number. */
	uint16_t var[OF_IR_VEC_SIZE];
	/* Component mask. */
	uint8_t mask;
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
	struct of_ir_ast_node *target;

	/** Opcode. */
	enum of_instr_opcode opc;

	/** Basic block to which the instruction belongs. */
	struct of_ir_ast_node *node;
	/** List head to link all instructions of the block. */
	struct list_head list;
};

/** Representation of AST node. */
struct of_ir_ast_node {
	/** List of subnodes of the block. */
	struct list_head nodes;
	/** Number of instructions in the block. */
	unsigned num_nodes;
	/** Parent of this node. */
	struct of_ir_ast_node *parent;
	/** List head used to link with the parent. */
	struct list_head parent_list;

	enum of_ir_node_type type;
	union {
		struct {
			struct of_ir_register *reg;
		} if_then;
		struct {
			struct of_ir_ast_node *region;
		} depart_repeat;
		struct {
			unsigned num_instrs;
			struct list_head instrs;
		} list;
	};

	union {
		struct {
			uint32_t *vars_defined;
			unsigned depart_count;
			unsigned depart_number;
			unsigned repeat_count;
			unsigned repeat_number;
			/** PHI() operators at the end of the block. */
			struct list_head phis;
			/** PHI() operators at the beginning of the block. */
			struct list_head loop_phis;
		} ssa;
		/* Data private to single stage of processing. */
	};

	/** Shader to which the AST node belongs. */
	struct of_ir_shader *shader;

	/* Address assigned by assembler. */
	unsigned address;

	/* Various data for processing algorithms */
	unsigned long priv_data;
};

/** Representation of a shader program. */
struct of_ir_shader {
	/** Total number of generated instructions. */
	unsigned num_instrs;
	/** List of root AST nodes in the program. */
	struct list_head root_nodes;

	/** Heap to allocate IR data from. */
	struct of_heap *heap;

	struct {
		unsigned num_vars;
	} stats;

	const struct of_ir_reg_info *reg_info;
};

void of_ir_dump_ast(struct of_ir_shader *shader, dump_ast_callback_t extra,
		    void *extra_data);

int of_ir_to_ssa(struct of_ir_shader *shader);
int of_ir_optimize(struct of_ir_shader *shader);

#endif /* OF_IR_PRIV_H_ */
