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

#include <util/u_dynarray.h>
#include <util/u_slab.h>

#include "openfimg_ir.h"
#include "openfimg_util.h"

/**
 * Runs specified shader tree processing pass.
 * @param shader Shader object to process.
 * @param data A pointer to data used by specified pass.
 * @param pass Pass function.
 */
#define RUN_PASS(shader, data, pass)					\
	do {								\
		struct of_ir_ast_node *node;				\
									\
		LIST_FOR_EACH_ENTRY(node, &(shader)->root_nodes, parent_list) \
			pass(data, node);				\
	} while (0)

/**
 * Callback function printing extended information about node.
 * @param shader Shader object being printed.
 * @param node Node to print.
 * @param level Number of characters of indentation level.
 * @param post True if being called after printing main data.
 * @param data Callback specific data pointer.
 */
typedef void (*dump_ast_callback_t)(struct of_ir_shader *shader,
				    struct of_ir_ast_node *node, unsigned level,
				    bool post, void *data);

struct of_ir_chunk;

/**
 * Structure describing a variable.
 * Not all fields are used by all processing stages.
 */
struct of_ir_variable {
	struct of_ir_chunk *chunk;		/**< Coalescer chunk to which the variable belongs. */
	struct of_ir_instruction *def_ins;	/**< Instruction defining the variable. */
	struct of_ir_phi *def_phi;		/**< PHI operator defining the variable. */
	uint32_t *interference;			/**< Interference bitmap. */
	unsigned constraints;			/**< Mask of constraint types. */
	unsigned color;				/**< Assigned color. */
	uint8_t parity;				/**< Assigned register parity. */
	uint8_t comp;				/**< Assigned component. */
	unsigned fixed :1;			/**< True if fixed to assigned register. */
};

struct of_ir_var_map;

/**
 * Structure containing internal data used by processing stages.
 * Not all fields are used by all processing stages.
 */
struct of_ir_optimizer {
	/* Common fields */
	struct of_ir_shader *shader;		/**< Shader object. */
	struct of_heap *heap;			/**< Heap to allocate from. */
	struct util_dynarray vars;		/**< Array of variables. */
	unsigned num_vars;			/**< Number of variables. */
	struct of_stack *renames_stack;		/**< Stack of rename dictionaries. */
	uint16_t *renames;			/**< Current rename dictionary. */
	bool want_interference;			/**< True if liveness should calculate interferences. */
	struct util_slab_mempool live_slab;	/**< Memory pool for liveness bitmaps. */
	uint32_t *live;				/**< Current liveness bitmap. */

	/* Fields used by SSA generator. */
	unsigned vars_bitmap_size;		/**< Bytes needed for bitmap of variables. */
	struct of_heap *shader_heap;		/**< Heap to allocate data for shader. */
	uint16_t last_var;			/**< Highest found variable number. */

	/* Fields used by optimizer. */
	struct of_stack *maps_stack;		/**< Stack of copy propagation maps. */
	struct of_ir_var_map *maps;		/**< Current copy propagation map. */

	/* Fields used by register allocator. */
	struct util_slab_mempool valset_slab;	/**< Memory pool for value sets. */
	struct util_slab_mempool chunk_slab;	/**< Memory pool for coalescer chunks. */
	struct list_head chunks;		/**< List of all coalescer chunks. */
	struct util_dynarray constraints;	/**< Array of allocation constraints. */
	unsigned num_constraints;		/**< Number of allocation constraints. */
	struct util_dynarray affinities;	/**< Array of coalescer affinities. */
	unsigned num_affinities;		/**< Number of coalescer affinities. */
	uint32_t *reg_bitmap[4];		/**< Bitmaps of assigned colors. */
	uint32_t *chunk_interf;			/**< Bitmap of chunk interferences. */
	struct util_dynarray chunk_queue;	/**< Sorted array of coalescer chunks. */
	unsigned chunk_queue_len;		/**< Number of chunks in chunk_queue. */
	unsigned parity :1;			/**< Current parity for round robin assignment. */
};

/**
 * Representation of signle PHI operator.
 * Number of source variables is inferred from container node and allocated
 * properly at PHI creation time.
 */
struct of_ir_phi {
	struct list_head list;	/**< List head to store the object in lists. */
	unsigned dead :1;	/**< True if the operator was not found live. */
	uint16_t reg;		/**< Pre-SSA register number. */
	uint16_t dst;		/**< Destination variable. */
	uint16_t src[];		/**< Array of source variables. */
};

/** Representation of single register usage. */
struct of_ir_register {
	/** Register modifiers. */
	enum of_ir_reg_flags flags;
	/** Register number. */
	uint16_t num;
	/** Variable number. */
	uint16_t var[OF_IR_VEC_SIZE];
	/** Component mask. */
	uint8_t mask;
	/** Dead component mask. N'th bit is set if N'th component is dead. */
	uint8_t deadmask;
	/** Register channel swizzle(map)/mask. */
	uint8_t swizzle[4];
	/** Register type. */
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
	unsigned target;

	/** Operation identifier. */
	enum of_instr_opcode opc;

	/** Control flow node to which the instruction belongs. */
	struct of_ir_ast_node *node;
	/** List head to link all instructions of a node. */
	struct list_head list;
};

/** Representation of control flow node. */
struct of_ir_ast_node {
	/** List of subnodes of the node. */
	struct list_head nodes;
	/** Parent of this node. */
	struct of_ir_ast_node *parent;
	/** List head used to link with the parent. */
	struct list_head parent_list;

	/** Type of the node. */
	enum of_ir_node_type type;
	union {
		struct {
			/** Register containing if-then condition. */
			struct of_ir_register *reg;
		} if_then; /**< Fields specific to if-then nodes. */
		struct {
			/** Region to depart/repeat. */
			struct of_ir_ast_node *region;
		} depart_repeat; /**< Fields specific to depart/repeat nodes. */
		struct {
			/** List of instructions. */
			struct list_head instrs;
		} list; /**< Fields specific to list nodes. */
	};

	struct {
		/** List of variables defined by the node. */
		uint32_t *vars_defined;
		/** Departure count of the node. */
		unsigned depart_count;
		/** Departure identifier of the node. */
		unsigned depart_number;
		/** Repeat count of the node. */
		unsigned repeat_count;
		/** Repeat identifier of the node. */
		unsigned repeat_number;
		/**
		 * PHI() operators at the end of the node.
		 * Non-empty only for region nodes.
		 */
		struct list_head phis;
		/**
		 * PHI() operators at the beginning of the node.
		 * Non-empty only for region nodes.
		 */
		struct list_head loop_phis;
	} ssa; /**< Fields specific to SSA form. */

	/** Shader to which the AST node belongs. */
	struct of_ir_shader *shader;

	/** Node start address determined by assembler. */
	unsigned start_address;
	/**
	 * Node end address determined by assembler.
	 * This is the address of first instruction following the node.
	 */
	unsigned end_address;

	/** Bitmap of variables alive at entry of the node. */
	uint32_t *livein;
	/** Bitmap of variables alive at exit of the node. */
	uint32_t *liveout;
};

/** Representation of a shader program. */
struct of_ir_shader {
	enum of_shader_type type;

	/** List of root AST nodes in the program. */
	struct list_head root_nodes;

	/** Heap to allocate IR data from. */
	struct of_heap *heap;

	struct {
		unsigned num_vars;	/**< Number of variables in program. */
		unsigned num_instrs;	/**< Number of instructions in program. */
	} stats; /**< Statistics for various purposes. */

	/** Register type information array specific for this shader. */
	const struct of_ir_reg_info *reg_info;

	/** Output buffer to store binary code to. */
	struct pipe_resource *buffer;
};

/*
 * Control flow tree dump helpers.
 */

/**
 * Prints control flow tree of the program in human readable form.
 * @param shader Shader object to print.
 * @param extra Optional callback to print additional data.
 * @param extra_data Optional data for extra callback.
 * @param str Text to print as header.
 */
void of_ir_dump_ast(struct of_ir_shader *shader, dump_ast_callback_t extra,
		    void *extra_data, const char *str);

/**
 * Conditionally prints control flow tree of the program in human readable form.
 * The tree is printed only if OF_DBG_AST_DUMP flag is enabled. Useful to print
 * only if debugging data is desired.
 * @param shader Shader object to print.
 * @param extra Optional callback to print additional data.
 * @param extra_data Optional data for extra callback.
 * @param str Text to print as header.
 */
#define OF_IR_DUMP_AST(shader, extra, extra_data, str)		\
	do { if (of_mesa_debug & OF_DBG_AST_DUMP)		\
		of_ir_dump_ast(shader, extra, extra_data, str);	\
	} while (0)
/**
 * Conditionally prints control flow tree of the program in human readable form.
 * The tree is printed only if OF_DBG_AST_VDUMP flag is enabled. Useful to print
 * only if verbose debugging data is desired.
 * @param shader Shader object to print.
 * @param extra Optional callback to print additional data.
 * @param extra_data Optional data for extra callback.
 * @param str Text to print as header.
 */
#define OF_IR_DUMP_AST_VERBOSE(shader, extra, extra_data, str)	\
	do { if (of_mesa_debug & OF_DBG_AST_VDUMP)		\
		of_ir_dump_ast(shader, extra, extra_data, str);	\
	} while (0)

/*
 * Various helpers to work on intermediate representation.
 */

/**
 * Merges two sets of register modifier flags.
 * Resulting flags are a composition of inner and outer arguments, i.e.
 * result(reg) = outer(inner(reg)).
 * @param inner Inner set of modifiers.
 * @param outer Outer set of modifiers.
 * @return Composition of specified modifiers.
 */
enum of_ir_reg_flags of_ir_merge_flags(enum of_ir_reg_flags inner,
				       enum of_ir_reg_flags outer);

/*
 * Common optimization passes.
 */

/**
 * Liveness analysis pass function.
 * To be called using RUN_PASS() helper. Requires the program to be in SSA form.
 * @param opt Optimizer data.
 * @param node Node being processed.
 */
void liveness(struct of_ir_optimizer *opt, struct of_ir_ast_node *node);
/**
 * Liveness clean-up pass function.
 * To be called using RUN_PASS() helper after running analysis pass.
 * @param opt Optimizer data.
 * @param node Node being processed.
 */
void cleanup(struct of_ir_optimizer *opt, struct of_ir_ast_node *node);

/*
 * Variable management.
 */

/**
 * Calculates identifier of variable represented by variable object.
 * @param opt Optimizer data.
 * @param var Variable object.
 * @return Variable identifier.
 */
static INLINE uint16_t
var_num(struct of_ir_optimizer *opt, struct of_ir_variable *var)
{
	struct of_ir_variable *vars = util_dynarray_begin(&opt->vars);
	return var - vars;
}
/**
 * Gets variable object pointer for given variable identifier.
 * @param opt Optimizer data.
 * @param var Variable identifier.
 * @return Variable object pointer.
 */
static INLINE struct of_ir_variable *
get_var(struct of_ir_optimizer *opt, uint16_t var)
{
	assert(var < opt->num_vars);
	return util_dynarray_element(&opt->vars, struct of_ir_variable, var);
}
/**
 * Adds new variable to the program.
 * @param opt Optimizer data.
 * @return Object representing newly created variable.
 */
static INLINE struct of_ir_variable *
add_var(struct of_ir_optimizer *opt)
{
	static const struct of_ir_variable v = { 0, };

	util_dynarray_append(&opt->vars, struct of_ir_variable, v);
	++opt->num_vars;
	return util_dynarray_top_ptr(&opt->vars, struct of_ir_variable);
}
/**
 * Adds new variable to the program.
 * @param opt Optimizer data.
 * @return Identifier of newly created variable.
 */
static INLINE uint16_t
add_var_num(struct of_ir_optimizer *opt)
{
	return var_num(opt, add_var(opt));
}

/*
 * Register helpers.
 */

/**
 * Checks whether argument is a vector.
 * @param reg Register object representing instruction argument.
 * @return True if register uses more than one component.
 */
static INLINE bool
reg_is_vector(struct of_ir_register *reg)
{
	return reg->mask & (reg->mask - 1);
}
/**
 * Checks whether component of register object is used.
 * @param reg Register object to check.
 * @param comp Index of component to check.
 * @return True if component is used.
 */
static INLINE bool
reg_comp_used(struct of_ir_register *reg, unsigned comp)
{
	return reg->mask & BIT(comp);
}

#endif /* OF_IR_PRIV_H_ */
