/*
 * Copyright (C) 2014 Tomasz Figa <tomasz.figa@gmail.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <util/u_double_list.h>

#include "openfimg_ir_priv.h"
#include "openfimg_ssa.h"
#include "openfimg_util.h"

/*
 * Fast dominator tree algorithm running in O(|E|a(|E|,|V|)), where a(x1,x2)
 * is the inverse Ackermann function.
 *
 * See T. Lengauer, R. E. Tarjan, A Fast Algorithm for Finding Dominators
 * in a Flowgraph.
 */

struct dtree_vertex {
	struct of_ir_cf_block *block;

	unsigned num_succ;
	struct dtree_vtx_pred_binding {
		/** Nth successor of this vertex. */
		struct dtree_vertex *vtx;
		/** List head to use in pred_list of successor. */
		struct list_head list;
		/** Index to get from binding to vertex. */
		unsigned index;
	} succ[OF_IR_NUM_CF_TARGETS];
	/** List of predecessors of this vertex in dominator tree. */
	struct list_head pred_list;

	struct dtree_vtx_semi_binding {
		/* Semidominator of this vertex. */
		struct dtree_vertex *vtx;
		/** List head to use in semi_list of semidominator. */
		struct list_head list;
	} semi;
	/** List of vertices semidominated by this vertex. */
	struct list_head semi_list;

	/** List head for various purposes (e.g. DFS stack) */
	struct list_head list;

	/** Various variables used for dominator tree generation. */
	unsigned id;
	struct list_head vtx_list;
	struct dtree_vertex *parent;
	struct dtree_vertex *dom;
	struct dtree_vertex *label;
	struct dtree_vertex *ancestor;
	unsigned size;
	struct dtree_vertex *child;
};

struct dtree {
	struct list_head vertices;
	struct dtree_vertex *storage;
	struct dtree_vertex sentinel;
};

/* Few useful helper functions. */
static inline struct dtree_vertex *
dtree_vertex(struct list_head *item)
{
	return LIST_ENTRY(struct dtree_vertex, item, list);
}

static inline void
dvtx_semi_set(struct dtree_vertex *dvtx, struct dtree_vertex *semi)
{
	list_del(&dvtx->semi.list);
	dvtx->semi.vtx = semi;
	list_addtail(&dvtx->semi.list, &semi->semi_list);
}

static inline bool
dtree_vtx_valid(const struct dtree *dtree, const struct dtree_vertex *dvtx)
{
	assert(dvtx);
	return dvtx != &dtree->sentinel;
}

/* DFS to build spanning tree and initialize certain vertex variables. */
static void
dtree_dfs(struct of_ir_shader *shader, struct dtree *dtree)
{
	struct dtree_vertex *dvtx = dtree_vertex(list_head(&dtree->vertices));
	struct list_head stack;
	unsigned id = 1;
	unsigned i;

	LIST_INITHEAD(&stack);
	list_push(&dvtx->list, &stack);

	while (!LIST_IS_EMPTY(&stack)) {
		dvtx = dtree_vertex(list_pop(&stack));
		dvtx->id = id++;
		dvtx->label = dvtx;

		dvtx_semi_set(dvtx, dvtx);

		for (i = 0; i < dvtx->num_succ; ++i) {
			struct dtree_vertex *succ = dvtx->succ[i].vtx;

			if (dtree_vtx_valid(dtree, succ->semi.vtx))
				continue;

			succ->parent = dvtx;
			list_push(&succ->list, &stack);
		}
	}
}

/* Main parts of fast dominator tree algorithm. */
static void
dtree_compress(struct dtree *dtree, struct dtree_vertex *dvtx)
{
	struct list_head stack;

	LIST_INITHEAD(&stack);
	list_push(&dvtx->list, &stack);

	while (!LIST_IS_EMPTY(&stack)) {
		dvtx = dtree_vertex(list_head(&stack));

		if (!dtree_vtx_valid(dtree, dvtx->ancestor->ancestor))
			break;

		list_push(&dvtx->ancestor->list, &stack);
	}

	while (!LIST_IS_EMPTY(&stack)) {
		dvtx = dtree_vertex(list_pop(&stack));

		if (dvtx->ancestor->label->semi.vtx->id
		    < dvtx->label->semi.vtx->id)
			dvtx->label = dvtx->ancestor->label;

		dvtx->ancestor = dvtx->ancestor->ancestor;
	}
}

static struct dtree_vertex *
dtree_eval(struct dtree *dtree, struct dtree_vertex *dvtx)
{
	if (!dtree_vtx_valid(dtree, dvtx->ancestor))
		return dvtx->label;

	dtree_compress(dtree, dvtx);

	if (dvtx->ancestor->label->semi.vtx->id >= dvtx->label->semi.vtx->id)
		return dvtx->label;

	return dvtx->ancestor->label;
}

static void
dtree_link(struct dtree *dtree, struct dtree_vertex *parent,
	   struct dtree_vertex *dvtx)
{
	struct dtree_vertex *child;

	while (dvtx->label->semi.vtx->id < child->child->label->semi.vtx->id) {
		if (child->size + child->child->child->size
		    >= 2 * child->child->size) {
			child->child->parent = child;
			child->child = child->child->child;
		} else {
			child->parent = child->child;
			child = child->parent;
		}

		child->label = dvtx->label;
		parent->size += dvtx->size;

		if (parent->size < 2 * dvtx->size)
			swap(child, parent->child, struct dtree_vertex *);

		while (dtree_vtx_valid(dtree, child)) {
			child->parent = parent;
			child = child->child;
		}
	}
}

static void
dtree_process(struct of_ir_shader *shader, struct dtree *dtree)
{
	struct list_head *first = list_head(&dtree->vertices);
	struct list_head *last = list_tail(&dtree->vertices);
	struct dtree_vertex *dvtx;

	dtree_dfs(shader, dtree);

	if (first == last)
		/* Nothing to do. */
		return;

	/* Iterate from last to second vertex. */
	LIST_FOR_EACH_ENTRY_FROM_REV(dvtx, last, first, vtx_list) {
		struct dtree_vertex *parent = dvtx->parent;
		struct dtree_vtx_pred_binding *predb;
		struct dtree_vtx_semi_binding *semib;

		LIST_FOR_EACH_ENTRY(predb, &dvtx->pred_list, list) {
			struct dtree_vertex *pred, *min;

			pred = container_of(predb, dvtx, succ[predb->index]);

			min = dtree_eval(dtree, pred);
			if (min->semi.vtx->id < dvtx->semi.vtx->id)
				dvtx_semi_set(dvtx, min->semi.vtx);
		}

		dtree_link(dtree, parent, dvtx);

		LIST_FOR_EACH_ENTRY(semib, &parent->semi_list, list) {
			struct dtree_vertex *semi, *min;

			semi = container_of(semib, dvtx, semi);
			list_del(&semi->semi.list);

			min = dtree_eval(dtree, semi);
			if (min->semi.vtx->id < semi->semi.vtx->id)
				semi->dom = min;
			else
				semi->dom = parent;
		}
	}

	/* Iterate from second to last vertex. */
	LIST_FOR_EACH_ENTRY_FROM(dvtx, first->next, &dtree->vertices, vtx_list)
		if (dvtx->dom != dvtx->semi.vtx)
			dvtx->dom = dvtx->dom->dom;
}

/* Tree initialization/clean-up. */
static inline struct dtree_vertex *
dtree_vtx_init(struct dtree *dtree, struct dtree_vertex *dvtx,
	       struct of_ir_cf_block *cf)
{
	unsigned i;

	dvtx->block = cf;

	if (cf)
		dvtx->num_succ = cf->num_targets;
	for (i = 0; i < ARRAY_SIZE(dvtx->succ); ++i) {
		dvtx->succ[i].vtx = &dtree->sentinel;
		LIST_INITHEAD(&dvtx->succ[i].list);
		dvtx->succ[i].index = i;
	}
	LIST_INITHEAD(&dvtx->pred_list);

	dvtx->semi.vtx = &dtree->sentinel;
	LIST_INITHEAD(&dvtx->semi.list);
	LIST_INITHEAD(&dvtx->semi_list);

	LIST_INITHEAD(&dvtx->list);

	LIST_INITHEAD(&dvtx->vtx_list);
	dvtx->parent = &dtree->sentinel;
	dvtx->dom = &dtree->sentinel;
	dvtx->label = &dtree->sentinel;
	dvtx->ancestor = &dtree->sentinel;
	dvtx->child = &dtree->sentinel;

	return dvtx;
}

static int
dtree_create(struct of_ir_shader *shader, struct dtree *dtree)
{
	struct of_ir_cf_block *cf;
	unsigned dtree_ptr = 0;
	unsigned i;

	dtree->storage = CALLOC(shader->num_cf_blocks, sizeof(*dtree->storage));
	if (!dtree->storage)
		return -1;
	LIST_INITHEAD(&dtree->vertices);
	dtree_vtx_init(dtree, &dtree->sentinel, NULL);

	LIST_FOR_EACH_ENTRY(cf, &shader->cf_blocks, list)
		cf->priv_data = -1UL;

	LIST_FOR_EACH_ENTRY(cf, &shader->cf_blocks, list) {
		struct dtree_vertex *dvtx;

		if (cf->priv_data != -1UL)
			continue;

		cf->priv_data = dtree_ptr;

		dvtx = dtree_vtx_init(dtree, &dtree->storage[dtree_ptr++], cf);

		for (i = 0; i < dvtx->num_succ; ++i) {
			struct dtree_vertex *succ;
			struct of_ir_cf_block *succ_cf;

			succ_cf = cf->targets[i].block;
			if (succ_cf->priv_data == -1UL) {
				succ_cf->priv_data = dtree_ptr;
				succ = dtree_vtx_init(dtree,
					&dtree->storage[dtree_ptr++], succ_cf);

			} else {
				succ = &dtree->storage[succ_cf->priv_data];
			}

			dvtx->succ[i].vtx = succ;
			list_addtail(&dvtx->succ[i].list,
					&succ->pred_list);
		}
	}

	dtree_process(shader, dtree);

	return 0;
}

static void
dtree_free(struct dtree *dtree)
{
	LIST_INITHEAD(&dtree->vertices);
	FREE(dtree->storage);
}

/*
 * Dominance frontier algorithm.
 *
 * See Ron Cytron et al., Efficiently Computing Static Single Assignment Form
 * and the Control Dependence Graph.
 */

// TODO

/*
 * Conversion of programs stored in OpenFIMG IR form into SSA form and back.
 *
 * To convert into SSA form, the following steps are performed:
 *   1) the fast dominator algorithm is used to find dominators tree of the
 *      flow graph,
 *   2) dominance frontier algorithm is used to find basic blocks where PSI
 *      functions need to be inserted,
 *   3) trivial PSI functions are inserted into determined set of basic blocks,
 *      with all their arguments pointing to the same variable,
 *   4) assignments to the same variable are replaced with assignments to
 *      new variables and respective arguments of affected PSI functions
 *      are changed to reflect this, leading to code in SSA form.
 *
 * To convert from SSA form into valid assembly, the following steps are
 * performed:
 *   1) TODO
 */

int
of_ssa_ir_to_ssa(struct of_ir_shader *shader)
{
	struct dtree dtree;
	int ret;

	assert(!LIST_IS_EMPTY(&shader->cf_blocks));

	ret = dtree_create(shader, &dtree);
	if (!ret)
		return -1;

	// TODO: Use finished dominator tree here

	dtree_free(&dtree);

	return 0;
}

int
of_ssa_ssa_to_ir(struct of_ir_shader *shader)
{
	// TODO
	return -1;
}
