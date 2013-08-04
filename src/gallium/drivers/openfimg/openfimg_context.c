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

#include "util/u_format.h"
#include "util/u_blit.h"

#include "openfimg_context.h"
#include "openfimg_draw.h"
#include "openfimg_emit.h"
#include "openfimg_fence.h"
#include "openfimg_resource.h"
#include "openfimg_texture.h"
#include "openfimg_state.h"
#include "openfimg_vertex.h"
#include "openfimg_util.h"

#include "compiler/openfimg_program.h"

static struct fd_ringbuffer *next_rb(struct of_context *ctx)
{
	struct fd_ringbuffer *ring;
	uint32_t ts;

	/* grab next ringbuffer: */
	ring = ctx->rings[(ctx->rings_idx++) % ARRAY_SIZE(ctx->rings)];

	/* wait for new rb to be idle: */
	ts = fd_ringbuffer_timestamp(ring);
	if (ts) {
		DBG("wait: %u", ts);
		fd_pipe_wait(ctx->pipe, ts);
	}

	fd_ringbuffer_reset(ring);

	return ring;
}

static void
of_context_next_rb(struct pipe_context *pctx)
{
	struct of_context *ctx = of_context(pctx);
	struct fd_ringbuffer *ring;

	fd_ringmarker_del(ctx->draw_start);
	fd_ringmarker_del(ctx->draw_end);

	ring = next_rb(ctx);

	ctx->draw_start = fd_ringmarker_new(ring);
	ctx->draw_end = fd_ringmarker_new(ring);

	fd_ringbuffer_set_parent(ring, NULL);
	ctx->ring = ring;
}

/* emit accumulated render cmds, needed for example if render target has
 * changed, or for flush()
 */
void
of_context_render(struct pipe_context *pctx)
{
	struct of_context *ctx = of_context(pctx);
	struct pipe_framebuffer_state *pfb = &ctx->framebuffer.base;
	unsigned i;

	VDBG("needs_flush: %d", ctx->needs_flush);

	if (!ctx->needs_flush)
		return;

	fd_ringmarker_mark(ctx->draw_end);
	VDBG("rendering sysmem (%s/%s)",
			util_format_short_name(pipe_surface_format(pfb->cbufs[0])),
			util_format_short_name(pipe_surface_format(pfb->zsbuf)));
	fd_ringmarker_flush(ctx->draw_start);
	fd_ringmarker_mark(ctx->draw_start);

	ctx->last_timestamp = fd_ringbuffer_timestamp(ctx->ring);

	VDBG("%p/%p/%p", ctx->ring->start, ctx->ring->cur,
		ctx->ring->end);

	/* if size in dwords is more than half the buffer size, then wait and
	 * wrap around:
	 */
	if ((ctx->ring->cur - ctx->ring->start) > ctx->ring->size/8)
		of_context_next_rb(pctx);

	ctx->needs_flush = false;
	ctx->cleared = ctx->restore = ctx->resolve = 0;
	ctx->num_draws = 0;

	if (pfb->cbufs[0])
		of_resource(pfb->cbufs[0]->texture)->dirty = false;
	if (pfb->zsbuf)
		of_resource(pfb->zsbuf->texture)->dirty = false;

	for (i = 0; i < ctx->num_pending_rsrcs; ++i)
		pipe_resource_reference(&ctx->pending_rsrcs[i], NULL);
	ctx->num_pending_rsrcs = 0;

	ctx->dirty |= OF_DIRTY_FRAMEBUFFER | OF_DIRTY_VERTTEX
			| OF_DIRTY_FRAGTEX;

	++ctx->draw_ticks;
	if (!(ctx->draw_ticks % 8))
		of_draw_cache_gc(ctx);
}

static void
of_context_flush(struct pipe_context *pctx, struct pipe_fence_handle **fence,
		unsigned flags)
{
	VDBG("fence=%p", fence);

	of_context_render(pctx);
#if 0
	if (fence) {
		struct of_context *ctx = of_context(pctx);

		of_fence_new(ctx, ctx->last_timestamp,
				(struct of_fence **)fence);
	}
#endif
}

void
of_context_destroy(struct pipe_context *pctx)
{
	struct of_context *ctx = of_context(pctx);
	int i;

	DBG("");

	if (ctx->pipe)
		fd_pipe_del(ctx->pipe);

	if (ctx->blitter)
		util_blitter_destroy(ctx->blitter);

	fd_ringmarker_del(ctx->draw_start);
	fd_ringmarker_del(ctx->draw_end);

	for (i = 0; i < ARRAY_SIZE(ctx->rings); ++i)
		fd_ringbuffer_del(ctx->rings[i]);

	of_draw_fini(pctx);
	of_program_fini(pctx);

	FREE(ctx);
}

void
of_context_init_solid(struct of_context *ctx)
{
	of_program_init_solid(ctx);

	ctx->clear_vertex_info = of_draw_init_solid(ctx);
	if (!ctx->clear_vertex_info)
		DBG("failed to create clear vertex info");
}

void
of_context_init_blit(struct of_context *ctx)
{
	of_program_init_blit(ctx);
}

static const uint8_t fimg_3dse_primtypes[PIPE_PRIM_MAX] = {
	[PIPE_PRIM_POINTS]         = PTYPE_POINTS,
	[PIPE_PRIM_LINES]          = PTYPE_LINES,
	[PIPE_PRIM_LINE_STRIP]     = PTYPE_LINE_STRIP,
	[PIPE_PRIM_TRIANGLES]      = PTYPE_TRIANGLES,
	[PIPE_PRIM_TRIANGLE_STRIP] = PTYPE_TRIANGLE_STRIP,
	[PIPE_PRIM_TRIANGLE_FAN]   = PTYPE_TRIANGLE_FAN,
};

struct pipe_context *
of_context_create(struct pipe_screen *pscreen, void *priv)
{
	struct of_context *ctx = CALLOC_STRUCT(of_context);
	struct of_screen *screen = of_screen(pscreen);
	struct pipe_context *pctx;
	int i;

	if (!ctx)
		return NULL;

	pctx = &ctx->base;

	ctx->pipe = fd_pipe_new(screen->dev, FD_PIPE_3D);
	if (!ctx->pipe) {
		DBG("could not create 3d pipe");
		goto fail;
	}

	ctx->screen = screen;

	ctx->primtypes = fimg_3dse_primtypes;
	ctx->primtype_mask = 0;
	for (i = 0; i < PIPE_PRIM_MAX; i++)
		if (fimg_3dse_primtypes[i])
			ctx->primtype_mask |= (1 << i);

	/* need some sane default in case state tracker doesn't
	 * set some state:
	 */
	ctx->sample_mask = 0xffff;

	pctx = &ctx->base;
	pctx->screen = pscreen;
	pctx->priv = priv;
	pctx->flush = of_context_flush;
	pctx->destroy = of_context_destroy;

	for (i = 0; i < ARRAY_SIZE(ctx->rings); i++) {
		ctx->rings[i] = fd_ringbuffer_new(ctx->pipe, 1024 * 1024);
		if (!ctx->rings[i])
			goto fail;
	}

	of_context_next_rb(pctx);

	util_slab_create(&ctx->transfer_pool, sizeof(struct pipe_transfer),
			16, UTIL_SLAB_SINGLETHREADED);

	of_draw_init(pctx);
	of_resource_context_init(pctx);
	of_texture_init(pctx);
	of_program_init(pctx);
	of_state_init(pctx);

	ctx->blitter = util_blitter_create(pctx);
	if (!ctx->blitter)
		goto fail;

	of_emit_setup(ctx);

	return pctx;

fail:
	pctx->destroy(pctx);
	return NULL;
}
