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

#include "openfimg_context.h"
#include "openfimg_util.h"

#include "fimg_3dse.xml.h"

/*
 * Low level intermediate representation of a FIMG-3DSE shader program.
 */

enum {
	OF_IR_NUM_CF_TARGETS = 2,
	OF_IR_NUM_SRCS = 3,
};

enum of_ir_shader_type {
	OF_IR_SHADER_VERTEX,
	OF_IR_SHADER_PIXEL,
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
	/** Virtual variable. */
	OF_IR_REG_VAR,

	OF_IR_NUM_REG_TYPES
};

enum of_ir_instr_type {
	OF_IR_CF,
	OF_IR_SUB,
	OF_IR_ALU,
};

enum of_ir_reg_flags {
	OF_IR_REG_NEGATE = (1 << 0),
	OF_IR_REG_ABS = (1 << 1),
	OF_IR_REG_SAT = (1 << 2),
};

enum of_ir_instr_flags {
	OF_IR_INSTR_NEXT_3SRC = (1 << 0),
};

struct of_ir_register;
struct of_ir_instruction;
struct of_ir_ast_node;
struct of_ir_shader;

struct of_ir_opc_info {
	const char *name;
	enum of_ir_instr_type type;
	unsigned num_srcs;
};

struct of_ir_instr_template {
	enum of_instr_opcode opc;
	union {
		/* ALU destination register. */
		struct {
			struct of_ir_register *reg;
			const char *mask;
			unsigned flags;
		} dst;
		/* Target block of subroutine call. */
		struct {
			struct of_ir_ast_node *node;
		} target;
	};
	struct {
		struct of_ir_register *reg;
		const char *swizzle;
		unsigned flags;
	} src[OF_IR_NUM_SRCS];
};

enum of_ir_node_type {
	OF_IR_NODE_REGION,
	OF_IR_NODE_IF_THEN,
	OF_IR_NODE_DEPART,
	OF_IR_NODE_REPEAT,
	OF_IR_NODE_LIST
};

struct of_ir_reg_info {
	const char *name;

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

extern const struct of_ir_opc_info of_ir_opc_info[];

static inline const struct of_ir_opc_info *
of_ir_get_opc_info(enum of_instr_opcode opc)
{
	return &of_ir_opc_info[opc];
}

const struct of_ir_reg_info *of_ir_get_reg_info(struct of_ir_shader *shader,
						enum of_ir_reg_type reg);
struct of_ir_register *of_ir_reg_create(struct of_ir_shader *shader,
					enum of_ir_reg_type type, unsigned num,
					const char *swizzle, unsigned flags);
struct of_ir_register *of_ir_reg_clone(struct of_ir_shader *shader,
				       struct of_ir_register *reg);

struct of_ir_instruction *of_ir_instr_create(struct of_ir_shader *shader,
					     enum of_instr_opcode opc);
void of_ir_instr_add_dst(struct of_ir_instruction *instr,
			 struct of_ir_register *reg);
void of_ir_instr_add_src(struct of_ir_instruction *instr,
			 struct of_ir_register *reg);
void of_ir_instr_insert(struct of_ir_shader *shader,
			struct of_ir_ast_node *block,
			struct of_ir_instruction *where,
			struct of_ir_instruction *instr);
struct of_ir_instruction *of_ir_instr_ptr(struct of_ir_shader *shader);

void of_ir_instr_insert_templ(struct of_ir_shader *shader,
			      struct of_ir_ast_node *block,
			      struct of_ir_instruction *where,
			      const struct of_ir_instr_template *instrs,
			      unsigned num_instrs);

struct of_ir_ast_node *of_ir_node_region(struct of_ir_shader *shader);
struct of_ir_ast_node *of_ir_node_if_then(struct of_ir_shader *shader,
					  struct of_ir_register *reg,
					  const char *swizzle, unsigned flags);
struct of_ir_ast_node *of_ir_node_depart(struct of_ir_shader *shader,
					 struct of_ir_ast_node *region);
struct of_ir_ast_node *of_ir_node_repeat(struct of_ir_shader *shader,
					 struct of_ir_ast_node *region);
struct of_ir_ast_node *of_ir_node_list(struct of_ir_shader *shader);
void of_ir_node_insert(struct of_ir_ast_node *where,
		       struct of_ir_ast_node *node);
enum of_ir_node_type of_ir_node_get_type(struct of_ir_ast_node *node);
struct of_ir_ast_node *of_ir_node_get_parent(struct of_ir_ast_node *node);

struct of_ir_shader *of_ir_shader_create(enum of_ir_shader_type type);
void of_ir_shader_destroy(struct of_ir_shader *shader);
int of_ir_shader_assemble(struct of_context *ctx, struct of_ir_shader *shader,
			  struct of_shader_stateobj *so);

#endif /* OF_IR_H_ */
