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

#ifndef FREEDRENO_CONTEXT_H_
#define FREEDRENO_CONTEXT_H_

#include "draw/draw_context.h"
#include "pipe/p_context.h"
#include "util/u_blitter.h"
#include "util/u_slab.h"
#include "util/u_string.h"

#include "openfimg_screen.h"

struct of_vertex_stateobj;

struct of_texture_stateobj {
	struct pipe_sampler_view *textures[PIPE_MAX_SAMPLERS];
	unsigned num_textures;
	struct pipe_sampler_state *samplers[PIPE_MAX_SAMPLERS];
	unsigned num_samplers;
	unsigned dirty_samplers;
};

struct of_program_stateobj {
	void *vp, *fp;
	enum {
		OF_SHADER_DIRTY_VP = (1 << 0),
		OF_SHADER_DIRTY_FP = (1 << 1),
	} dirty;
	uint8_t num_exports;
	/* Indexed by semantic name or TGSI_SEMANTIC_COUNT + semantic index
	 * for TGSI_SEMANTIC_GENERIC.  Special vs exports (position and point-
	 * size) are not included in this
	 */
	uint8_t export_linkage[63];
};

struct of_constbuf_stateobj {
	struct pipe_constant_buffer cb[PIPE_MAX_CONSTANT_BUFFERS];
	uint32_t enabled_mask;
	uint32_t dirty_mask;
};

struct of_vertexbuf_stateobj {
	struct pipe_vertex_buffer vb[PIPE_MAX_ATTRIBS];
	unsigned count;
	uint32_t enabled_mask;
	uint32_t dirty_mask;
};

struct of_vertex_stateobj {
	struct pipe_vertex_element pipe[PIPE_MAX_ATTRIBS];
	unsigned num_elements;
};

struct of_context {
	struct pipe_context base;

	struct of_screen *screen;
	struct blitter_context *blitter;

	struct util_slab_mempool transfer_pool;

	/* shaders used by clear, and gmem->mem blits: */
	struct of_program_stateobj solid_prog; // TODO move to screen?

	/* shaders used by mem->gmem blits: */
	struct of_program_stateobj blit_prog; // TODO move to screen?

	/* do we need to mem2gmem before rendering.  We don't, if for example,
	 * there was a glClear() that invalidated the entire previous buffer
	 * contents.  Keep track of which buffer(s) are cleared, or needs
	 * restore.  Masks of PIPE_CLEAR_*
	 */
	enum {
		/* align bitmask values w/ PIPE_CLEAR_*.. since that is convenient.. */
		OF_BUFFER_COLOR   = PIPE_CLEAR_COLOR,
		OF_BUFFER_DEPTH   = PIPE_CLEAR_DEPTH,
		OF_BUFFER_STENCIL = PIPE_CLEAR_STENCIL,
		OF_BUFFER_ALL     = OF_BUFFER_COLOR | OF_BUFFER_DEPTH | OF_BUFFER_STENCIL,
	} cleared, restore, resolve;

	bool needs_flush;
	unsigned num_draws;

	struct of_ringbuffer *ring;
	struct of_ringmarker *draw_start, *draw_end;

	struct pipe_scissor_state scissor;

	/* we don't have a disable/enable bit for scissor, so instead we keep
	 * a disabled-scissor state which matches the entire bound framebuffer
	 * and use that when scissor is not enabled.
	 */
	struct pipe_scissor_state disabled_scissor;

	/* Track the maximal bounds of the scissor of all the draws within a
	 * batch.  Used at the tile rendering step (of_gmem_render_tiles(),
	 * mem2gmem/gmem2mem) to avoid needlessly moving data in/out of gmem.
	 */
	struct pipe_scissor_state max_scissor;

	/* which state objects need to be re-emit'd: */
	enum {
		OF_DIRTY_BLEND       = (1 <<  0),
		OF_DIRTY_RASTERIZER  = (1 <<  1),
		OF_DIRTY_ZSA         = (1 <<  2),
		OF_DIRTY_FRAGTEX     = (1 <<  3),
		OF_DIRTY_VERTTEX     = (1 <<  4),
		OF_DIRTY_TEXSTATE    = (1 <<  5),
		OF_DIRTY_PROG        = (1 <<  6),
		OF_DIRTY_BLEND_COLOR = (1 <<  7),
		OF_DIRTY_STENCIL_REF = (1 <<  8),
		OF_DIRTY_SAMPLE_MASK = (1 <<  9),
		OF_DIRTY_FRAMEBUFFER = (1 << 10),
		OF_DIRTY_STIPPLE     = (1 << 11),
		OF_DIRTY_VIEWPORT    = (1 << 12),
		OF_DIRTY_CONSTBUF    = (1 << 13),
		OF_DIRTY_VTXSTATE    = (1 << 14),
		OF_DIRTY_VTXBUF      = (1 << 15),
		OF_DIRTY_INDEXBUF    = (1 << 16),
		OF_DIRTY_SCISSOR     = (1 << 17),
	} dirty;

	struct pipe_blend_state *blend;
	struct pipe_rasterizer_state *rasterizer;
	struct pipe_depth_stencil_alpha_state *zsa;

	struct of_texture_stateobj verttex, fragtex;

	struct of_program_stateobj prog;

	struct of_vertex_stateobj *vtx;

	uint32_t blend_color;
	struct pipe_stencil_ref stencil_ref;
	unsigned sample_mask;
	struct pipe_framebuffer_state framebuffer;
	struct pipe_poly_stipple stipple;
	struct pipe_viewport_state viewport;
	struct of_constbuf_stateobj constbuf[PIPE_SHADER_TYPES];
	struct of_vertexbuf_stateobj vertexbuf;
	struct pipe_index_buffer indexbuf;
};

static INLINE struct of_context *
of_context(struct pipe_context *pctx)
{
	return (struct of_context *)pctx;
}

static INLINE struct pipe_scissor_state *
of_context_get_scissor(struct of_context *ctx)
{
	if (ctx->rasterizer && ctx->rasterizer->scissor)
		return &ctx->scissor;
	return &ctx->disabled_scissor;
}

struct pipe_context * of_context_init(struct of_context *ctx,
		struct pipe_screen *pscreen, void *priv);

void of_context_render(struct pipe_context *pctx);

struct pipe_context * of_context_create(struct pipe_screen *pscreen,
		void *priv);

void of_context_destroy(struct pipe_context *pctx);

#endif /* FREEDRENO_CONTEXT_H_ */
