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

#define DEBUG_MSG(...)  do { if (0) DBG(__VA_ARGS__); } while (0)
#define WARN_MSG(...)   DBG("WARN:  "__VA_ARGS__)
#define ERROR_MSG(...)  DBG("ERROR: "__VA_ARGS__)

#define RUN_PASS(shader, data, pass)					\
	do {								\
		struct of_ir_ast_node *node;				\
									\
		LIST_FOR_EACH_ENTRY(node, &(shader)->root_nodes, parent_list) \
			pass(data, node);				\
	} while (0)

typedef void (*dump_ast_callback_t)(struct of_ir_shader *,
				    struct of_ir_ast_node *, unsigned, bool,
				    void *);

struct of_ir_chunk;

struct of_ir_variable {
	struct of_ir_chunk *chunk;
	struct of_ir_instruction *def_ins;
	struct of_ir_phi *def_phi;
	uint32_t *interference;
	unsigned constraints;
	unsigned color;
	uint8_t parity;
	uint8_t comp;
	unsigned fixed :1;
};

struct of_ir_var_map;

struct of_ir_optimizer {
	struct of_ir_shader *shader;
	struct of_heap *heap;
	struct util_dynarray vars;
	unsigned num_vars;
	struct of_stack *renames_stack;
	uint16_t *renames;
	bool want_interference;
	struct util_slab_mempool live_slab;
	uint32_t *live;

	/* Fields used by SSA generator. */
	unsigned vars_bitmap_size;
	struct of_heap *shader_heap;
	uint16_t last_var;

	/* Fields used by optimizer. */
	struct of_stack *maps_stack;
	struct of_ir_var_map *maps;

	/* Fields used by register allocator. */
	struct util_slab_mempool valset_slab;
	struct util_slab_mempool chunk_slab;
	struct list_head chunks;
	struct util_dynarray constraints;
	unsigned num_constraints;
	struct util_dynarray affinities;
	unsigned num_affinities;
	uint32_t *reg_bitmap[4];
	uint32_t *chunk_interf;
	struct util_dynarray chunk_queue;
	unsigned chunk_queue_len;
	unsigned parity :1;

	/* Fields used by assembler. */
	uint32_t *dwords;
	unsigned cur_instr;
};

struct of_ir_phi {
	struct list_head list;
	unsigned dead :1;
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
	uint8_t deadmask;
	/* Register channel swizzle(map)/mask. */
	uint8_t swizzle[4];
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
	unsigned target;

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
			struct list_head instrs;
		} list;
	};

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

	/** Shader to which the AST node belongs. */
	struct of_ir_shader *shader;

	/* Address assigned by assembler. */
	unsigned start_address;
	unsigned end_address;

	uint32_t *livein;
	uint32_t *liveout;
};

/** Representation of a shader program. */
struct of_ir_shader {
	/** List of root AST nodes in the program. */
	struct list_head root_nodes;

	/** Heap to allocate IR data from. */
	struct of_heap *heap;

	struct {
		unsigned num_vars;
		unsigned num_instrs;
	} stats;

	const struct of_ir_reg_info *reg_info;

	struct pipe_resource *buffer;
};

void of_ir_dump_ast(struct of_ir_shader *shader, dump_ast_callback_t extra,
		    void *extra_data, const char *str);

#define OF_IR_DUMP_AST(shader, extra, extra_data, str)		\
	do { if (of_mesa_debug & OF_DBG_AST_DUMP)		\
		of_ir_dump_ast(shader, extra, extra_data, str);	\
	} while (0)

#define OF_IR_DUMP_AST_VERBOSE(shader, extra, extra_data, str)	\
	do { if (of_mesa_debug & OF_DBG_AST_VDUMP)		\
		of_ir_dump_ast(shader, extra, extra_data, str);	\
	} while (0)

void of_ir_merge_flags(struct of_ir_register *reg, enum of_ir_reg_flags flags);

int of_ir_to_ssa(struct of_ir_shader *shader);
int of_ir_optimize(struct of_ir_shader *shader);
int of_ir_assign_registers(struct of_ir_shader *shader);
int of_ir_generate_code(struct of_context *ctx, struct of_ir_shader *shader);

/* Optimization passes: */
void liveness(struct of_ir_optimizer *opt, struct of_ir_ast_node *node);
void cleanup(struct of_ir_optimizer *opt, struct of_ir_ast_node *node);

/*
 * Variable management.
 */

static INLINE uint16_t
var_num(struct of_ir_optimizer *opt, struct of_ir_variable *var)
{
	struct of_ir_variable *vars = util_dynarray_begin(&opt->vars);
	return var - vars;
}

static INLINE struct of_ir_variable *
get_var(struct of_ir_optimizer *opt, uint16_t var)
{
	assert(var < opt->num_vars);
	return util_dynarray_element(&opt->vars, struct of_ir_variable, var);
}

static INLINE struct of_ir_variable *
add_var(struct of_ir_optimizer *opt)
{
	static const struct of_ir_variable v = { 0, };

	util_dynarray_append(&opt->vars, struct of_ir_variable, v);
	++opt->num_vars;
	return util_dynarray_top_ptr(&opt->vars, struct of_ir_variable);
}

static INLINE uint16_t
add_var_num(struct of_ir_optimizer *opt)
{
	return var_num(opt, add_var(opt));
}

/*
 * Register helpers.
 */

static INLINE bool
reg_is_vector(struct of_ir_register *reg)
{
	return reg->mask & (reg->mask - 1);
}

static INLINE bool
reg_comp_used(struct of_ir_register *reg, unsigned comp)
{
	return reg->mask & BIT(comp);
}

#endif /* OF_IR_PRIV_H_ */
