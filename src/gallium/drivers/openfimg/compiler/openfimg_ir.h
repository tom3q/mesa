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
	OF_IR_NUM_SRCS = 3,	/**< Maximum number of source arguments. */
	OF_IR_VEC_SIZE = 4,	/**< Number of components in vector. */
};

/** Register types used by intermediate representation. */
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
	/** Shader output register (write-only). */
	OF_IR_REG_O,
	/** Address register 0 (write-only). */
	OF_IR_REG_A0,
	/** Virtual variable. */
	OF_IR_REG_VAR,
	/** Virtual variable with assigned color. */
	OF_IR_REG_VARC,

	OF_IR_NUM_REG_TYPES
};

/** Types of instructions in intermediate representation. */
enum of_ir_instr_type {
	OF_IR_CF,	/**< Control flow instructions. */
	OF_IR_SUB,	/**< Subroutine control instructions. */
	OF_IR_ALU,	/**< Arithmetic instructions. */
};

/** Modifier flags for instruction arguments. */
enum of_ir_reg_flags {
	OF_IR_REG_NEGATE = (1 << 0),	/**< Negate source argument. */
	OF_IR_REG_ABS = (1 << 1),	/**< Drop sign of source argument. */
	OF_IR_REG_SAT = (1 << 2),	/**< Clamp destination argument to [0, 1]. */
};

/** Modifier flags for instructions. */
enum of_ir_instr_flags {
	OF_IR_INSTR_COPY = (1 << 1),		/**< Instruction copies a variable. */
	OF_IR_INSTR_DEAD = (1 << 2),		/**< Instruction is dead. */
};

struct of_ir_register;
struct of_ir_instruction;
struct of_ir_ast_node;
struct of_ir_shader;

/**
 * Source vector component mask. A string consisting of 4 characters,
 * where str[i] can be either '_' to mask i'th component or "xyzw"[i] to
 * mark it as active.
 */
typedef const char *src_mask_t;

/**
 * Map of source components taking part in calculation of destination vector
 * components.
 */
typedef src_mask_t dst_map_t[OF_IR_VEC_SIZE];

/** Structure containing information about operations supported by hardware. */
struct of_ir_opc_info {
	const char *name;		/**< Name for printing purposes. */
	enum of_ir_instr_type type;	/**< Operation type. */
	unsigned num_srcs;		/**< Number of source operands. */
	const dst_map_t *dst_map;	/**< Map of used source components. */
	bool fix_comp;			/**< Cannot handle destination swizzle. */
	bool replicated;		/**< Result is replicated in all channels. */
	bool tex;			/**< A texture sampling operation. */
};

/** Instruction template for easy code generation. */
struct of_ir_instr_template {
	enum of_instr_opcode opc;	/**< Opcode identifier. */
	union {
		struct {
			struct of_ir_register *reg;	/**< Register object. */
			const char *mask;		/**< Write mask. */
			unsigned flags;			/**< Modifiers. */
		} dst;		/**<  ALU destination register. */
		struct {
			struct of_ir_ast_node *node;	/**< Target node. */
		} target;	/**< Target of subroutine call. */
	};
	struct {
		struct of_ir_register *reg;		/**< Register object. */
		const char *swizzle;			/**< Component map. */
		unsigned flags;				/**< Modifiers. */
	} src[OF_IR_NUM_SRCS];	/**< Source registers. */
};

/** Supported types of control flow tree nodes. */
enum of_ir_node_type {
	OF_IR_NODE_REGION,	/**< Region. */
	OF_IR_NODE_IF_THEN,	/**< If-then. */
	OF_IR_NODE_DEPART,	/**< Depart. */
	OF_IR_NODE_REPEAT,	/**< Repeat. */
	OF_IR_NODE_LIST		/**< List. */
};

/**
 * Structure containing information about register types supported by
 * hardware.
 */
struct of_ir_reg_info {
	const char *name; /**< Printable name. */

	enum of_instr_src src_type; /**< Hardware source type identifier. */
	enum of_instr_dst dst_type; /**< Hardware destination type identifier. */

	unsigned num_reads;	/**< Allowed number of reads per instruction. */
	unsigned num_regs;	/**< Number of registers of this type. */

	bool writable :1;	/**< True if registers are writable. */
	bool readable :1;	/**< True if registers are readable. */
	bool scalar :1;		/**< True if registers are not vectors. */
	bool al_addr :1;	/**< True if can be addressed with AL register. */
	bool a0_addr :1;	/**< True if can be addressed with A0 register. */
};

extern const struct of_ir_opc_info of_ir_opc_info[];

/*
 * Helpers to retrieve various information.
 */

/**
 * Retrieves information about specified operation.
 * @param opc Operation identifier.
 * @return Structure containing information about specified operation.
 */
static inline const struct of_ir_opc_info *
of_ir_get_opc_info(enum of_instr_opcode opc)
{
	return &of_ir_opc_info[opc];
}
/**
 * Retrieves information about specified register type.
 * @param shader IR shader object.
 * @param reg Register type identifier.
 * @return Structure containing information about specified register type.
 */
const struct of_ir_reg_info *of_ir_get_reg_info(struct of_ir_shader *shader,
						enum of_ir_reg_type reg);

/*
 * Register level operations.
 */

/**
 * Creates an initialized register object.
 * @param shader Parent IR shader object.
 * @param type Register type identifier.
 * @param num Register number.
 * @param swizzle Register component map string.
 * @param flags Bit mask of register modifiers.
 * @return Requested register object or NULL on error.
 */
struct of_ir_register *of_ir_reg_create(struct of_ir_shader *shader,
					enum of_ir_reg_type type, unsigned num,
					const char *swizzle, unsigned flags);
/**
 * Creates a copy of existing register object.
 * @param shader Parent IR shader object.
 * @param reg Register object to copy.
 * @return Requested register object or NULL on error.
 */
struct of_ir_register *of_ir_reg_clone(struct of_ir_shader *shader,
				       struct of_ir_register *reg);

/*
 * Instruction level operations.
 */

/**
 * Creates an instruction object with specified operation.
 * @param shader Parent IR shader object.
 * @param opc Operation identifier.
 * @return Requested instruction object or NULL on error.
 */
struct of_ir_instruction *of_ir_instr_create(struct of_ir_shader *shader,
					     enum of_instr_opcode opc);
/**
 * Adds specified register as destination of specified instruction.
 * Note that only one destination can be added.
 * @param instr Instruction object to add destination to.
 * @param reg Register object to use as destination.
 */
void of_ir_instr_add_dst(struct of_ir_instruction *instr,
			 struct of_ir_register *reg);
/**
 * Adds specified register as source of specified instruction.
 * Note that subsequent calls will add further source registers up to
 * maximum number of source registers of given instruction.
 * @param instr Instruction object to add source to.
 * @param reg Register object to use as source.
 */
void of_ir_instr_add_src(struct of_ir_instruction *instr,
			 struct of_ir_register *reg);
/**
 * Inserts instruction into program.
 * Instruction is inserted at the end of specified node or after specified
 * preceeding instruction. Note that exactly one of node and where arguments
 * must be non-NULL.
 * @param shader Parent IR shader object.
 * @param node Control flow node to insert instruction at the end of, or NULL.
 * @param where Instruction after which given instruction should be inserted,
 * or NULL.
 * @param instr Instruction to be inserted.
 */
void of_ir_instr_insert(struct of_ir_shader *shader,
			struct of_ir_ast_node *node,
			struct of_ir_instruction *where,
			struct of_ir_instruction *instr);
/**
 * Inserts instruction into program.
 * Instruction is inserted at the begining of specified node or before specified
 * succeeding instruction. Note that exactly one of node and where arguments
 * must be non-NULL.
 * @param shader Parent IR shader object.
 * @param node Control flow node to insert instruction at the begining of,
 * or NULL.
 * @param where Instruction before which given instruction should be inserted,
 * or NULL.
 * @param instr Instruction to be inserted.
 */
void of_ir_instr_insert_before(struct of_ir_shader *shader,
			       struct of_ir_ast_node *node,
			       struct of_ir_instruction *where,
			       struct of_ir_instruction *instr);
/**
 * Constructs instructions according to specified array of instruction
 * templates and inserts them into program.
 * Instructions are inserted at the end of specified node or after specified
 * preceeding instruction. Note that exactly one of node and where arguments
 * must be non-NULL.
 * @param shader Parent IR shader object.
 * @param node Control flow node to insert instruction at the end of, or NULL.
 * @param where Instruction after which given instruction should be inserted,
 * or NULL.
 * @param instrs Initialized array of instruction templates.
 * @param num_instrs Number of elements in instrs array.
 */
void of_ir_instr_insert_templ(struct of_ir_shader *shader,
			      struct of_ir_ast_node *node,
			      struct of_ir_instruction *where,
			      const struct of_ir_instr_template *instrs,
			      unsigned num_instrs);

/*
 * Node level operations.
 */

/**
 * Creates control flow region node.
 * Initially the node is inserted as top level note of specified shader.
 * @param shader Parent shader object.
 * @return New region node or NULL on error.
 */
struct of_ir_ast_node *of_ir_node_region(struct of_ir_shader *shader);
/**
 * Creates control flow if-then node.
 * Initially the node is inserted as top level note of specified shader.
 * @param shader Parent shader object.
 * @param reg Register containing condition value.
 * @param swizzle Component map for condition register.
 * @param flags Modifiers for condition register.
 * @return New if-then node or NULL on error.
 */
struct of_ir_ast_node *of_ir_node_if_then(struct of_ir_shader *shader,
					  struct of_ir_register *reg,
					  const char *swizzle, unsigned flags);
/**
 * Creates control flow depart node.
 * Initially the node is inserted as top level note of specified shader.
 * @param shader Parent shader object.
 * @param region Region node to depart.
 * @return New depart node or NULL on error.
 */
struct of_ir_ast_node *of_ir_node_depart(struct of_ir_shader *shader,
					 struct of_ir_ast_node *region);
/**
 * Creates control flow repeat node.
 * Initially the node is inserted as top level note of specified shader.
 * @param shader Parent shader object.
 * @param region Region node to repeat.
 * @return New repeat node or NULL on error.
 */
struct of_ir_ast_node *of_ir_node_repeat(struct of_ir_shader *shader,
					 struct of_ir_ast_node *region);
/**
 * Creates control flow list node.
 * Initially the node is inserted as top level note of specified shader.
 * @param shader Parent shader object.
 * @return New list node or NULL on error.
 */
struct of_ir_ast_node *of_ir_node_list(struct of_ir_shader *shader);
/**
 * Inserts node at the end of another node.
 * @param where Node at the end of which to insert.
 * @param node Node to insert.
 */
void of_ir_node_insert(struct of_ir_ast_node *where,
		       struct of_ir_ast_node *node);
/**
 * Retrieves type of specified node.
 * @param node Node to retrieve type of.
 * @return Type of specified node.
 */
enum of_ir_node_type of_ir_node_get_type(struct of_ir_ast_node *node);
/**
 * Retrieves parent of specified node.
 * @param node Node to retrieve parent of.
 * @return Parent node of specified node or NULL if it is a top level node.
 */
struct of_ir_ast_node *of_ir_node_get_parent(struct of_ir_ast_node *node);

/* Helpers to insert instructions in certain parts of the code. */

/**
 * Returns a list node before specified node.
 * If there is no such node, the function will create it.
 * @param node Node to return list node before.
 * @return Requested list node.
 */
struct of_ir_ast_node *of_ir_node_list_before(struct of_ir_ast_node *node);
/**
 * Returns a list node at the begining of specified node.
 * If there is no such node, the function will create it.
 * @param node Node to return list node at begining of.
 * @return Requested list node.
 */
struct of_ir_ast_node *of_ir_node_list_front(struct of_ir_ast_node *node);
/**
 * Returns a list node at the end of specified node.
 * If there is no such node, the function will create it.
 * @param node Node to return list node at the end of.
 * @return Requested list node.
 */
struct of_ir_ast_node *of_ir_node_list_back(struct of_ir_ast_node *node);
/**
 * Returns a list node after specified node.
 * If there is no such node, the function will create it.
 * @param node Node to return list node after.
 * @return Requested list node.
 */
struct of_ir_ast_node *of_ir_node_list_after(struct of_ir_ast_node *node);

/*
 * Shader level operations.
 */

/**
 * Creates a new shader object.
 * @param type Shader unit type identifier.
 * @return New shader object or NULL on failure.
 */
struct of_ir_shader *of_ir_shader_create(enum of_shader_type type);
/**
 * Destroys specified shader object.
 * @param shader Shader object to destroy.
 */
void of_ir_shader_destroy(struct of_ir_shader *shader);

/*
 * IR processing stages
 */

/**
 * Performs conversion of intermediate representation to SSA form.
 * @param shader A shader object to convert.
 * @return Zero on success, non-zero on failure.
 */
int of_ir_to_ssa(struct of_ir_shader *shader);
/**
 * Performs optimization passes on intermediate representation of a program.
 * Requires the program to be in SSA form. Keeps the program in SSA form.
 * @param shader A shader object to optimize.
 * @return Zero on success, non-zero on failure.
 */
int of_ir_optimize(struct of_ir_shader *shader);
/**
 * Assigns registers to variables used in a program.
 * Requires the program to be in SSA form. Destroys SSA form of the program.
 * @param shader A shader object to assign registers in.
 * @return Zero on succees, non-zero on failure.
 */
int of_ir_assign_registers(struct of_ir_shader *shader);
/**
 * Generates binary code from intermediate representation of a program.
 * Requires the program to have registers assigned.
 * @param ctx Driver's pipe context.
 * @param shader A shader object containing the program to generate code from.
 * @param buffer Output buffer pointer to store resulting buffer with code.
 * @param num_instrs Pointer to variable to which resulting number of
 * instructions should be stored.
 * @return Zero on succees, non-zero on failure.
 */
int of_ir_generate_code(struct of_context *ctx, struct of_ir_shader *shader,
			struct pipe_resource **buffer, unsigned *num_instrs);

#endif /* OF_IR_H_ */
