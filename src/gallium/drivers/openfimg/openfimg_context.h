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

#ifndef OPENFIMG_CONTEXT_H_
#define OPENFIMG_CONTEXT_H_

#include "draw/draw_context.h"
#include "pipe/p_context.h"
#include "util/u_blitter.h"
#include <util/u_double_list.h>
#include "util/u_slab.h"
#include "util/u_string.h"
#include "indices/u_primconvert.h"

#include "openfimg_util.h"
#include "openfimg_screen.h"

#define OF_MAX_ATTRIBS		9

struct of_vertex_stateobj;
struct of_vertex_info;
struct of_draw_info;
struct of_shader_stateobj;

struct of_texture_stateobj {
	struct pipe_sampler_view *textures[PIPE_MAX_SAMPLERS];
	unsigned num_textures;
	struct pipe_sampler_state *samplers[PIPE_MAX_SAMPLERS];
	unsigned num_samplers;
	unsigned dirty_samplers;
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

struct of_framebuffer_stateobj {
	struct pipe_framebuffer_state base;
	uint32_t fgpf_fbctl;
};

struct of_cso_state {
	struct pipe_blend_state *blend;
	struct pipe_rasterizer_state *rasterizer;
	struct pipe_depth_stencil_alpha_state *zsa;
	struct of_vertex_stateobj *vtx;
	struct of_shader_stateobj *vp, *fp;
};

struct of_context {
	struct pipe_context base;

	struct fd_pipe *pipe;
	struct of_screen *screen;
	struct blitter_context *blitter;
	struct cso_hash *draw_hash;

	struct util_slab_mempool transfer_pool;

	/* table with PIPE_PRIM_MAX entries mapping PIPE_PRIM_x to
	 * DI_PT_x value to use for draw initiator.  There are some
	 * slight differences between generation:
	 */
	const uint8_t *primtypes;
	uint32_t primtype_mask;

	/* optional state used for hardware clear */
	struct of_shader_stateobj *solid_vp, *solid_fp; // TODO move to screen?
	struct of_vertex_info *clear_vertex_info;
	struct pipe_resource *dummy_shader;

	/* optional state used for hardware blitting */
	struct of_shader_stateobj *blit_vp, *blit_fp; // TODO move to screen?

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
	uint32_t last_timestamp;
	unsigned last_draw_mode;

	struct fd_ringbuffer *rings[2];
	unsigned rings_idx;

	struct fd_ringbuffer *ring;
	struct fd_ringmarker *draw_start, *draw_end;

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
		OF_DIRTY_PROG_VP     = (1 << 18),
		OF_DIRTY_PROG_FP     = (1 << 19),
	} dirty;

	struct of_cso_state cso;
	struct of_cso_state cso_active;

	struct of_texture_stateobj verttex, fragtex;

	struct of_draw_info *draw;

	uint32_t blend_color;
	struct pipe_stencil_ref stencil_ref;
	unsigned sample_mask;
	struct of_framebuffer_stateobj framebuffer;
	struct pipe_poly_stipple stipple;
	struct pipe_viewport_state viewport;
	struct of_constbuf_stateobj constbuf[PIPE_SHADER_TYPES];
	struct of_vertexbuf_stateobj vertexbuf;
	struct pipe_index_buffer indexbuf;
	struct pipe_resource *pending_rsrcs[512];
	unsigned num_pending_rsrcs;
};

static INLINE struct of_context *
of_context(struct pipe_context *pctx)
{
	return (struct of_context *)pctx;
}

static INLINE struct pipe_scissor_state *
of_context_get_scissor(struct of_context *ctx)
{
	if (ctx->cso.rasterizer && ctx->cso.rasterizer->scissor)
		return &ctx->scissor;
	return &ctx->disabled_scissor;
}

static INLINE bool
of_supported_prim(struct of_context *ctx, unsigned prim)
{
	return (1 << prim) & ctx->primtype_mask;
}

void of_context_render(struct pipe_context *pctx);

static INLINE void
of_reference_draw_buffer(struct of_context *ctx, struct pipe_resource *buffer)
{
	if (!buffer)
		return;

	if (ctx->num_pending_rsrcs == ARRAY_SIZE(ctx->pending_rsrcs))
		of_context_render(&ctx->base);

	pipe_resource_reference(&ctx->pending_rsrcs[ctx->num_pending_rsrcs++],
				buffer);
}

struct pipe_context * of_context_create(struct pipe_screen *pscreen,
		void *priv);

void of_context_destroy(struct pipe_context *pctx);

void of_context_init_solid(struct of_context *ctx);
void of_context_init_blit(struct of_context *ctx);

#endif /* OPENFIMG_CONTEXT_H_ */
