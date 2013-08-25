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

#include "openfimg_context.h"
#include "openfimg_draw.h"
#include "openfimg_resource.h"
#include "openfimg_texture.h"
#include "openfimg_state.h"
#include "openfimg_util.h"

/* there are two cases where we currently need to wait for render complete:
 * 1) pctx->flush() .. since at the moment we have no way for DDX to sync
 *    the presentation blit with the 3d core
 * 2) wrap-around for ringbuffer.. possibly we can do something more
 *    Intelligent here.  Right now we need to ensure there is enough room
 *    at the end of the drawcmds in the cmdstream buffer for all the per-
 *    tile cmds.  We do this the lamest way possible, by making the ringbuffer
 *    big, and flushing and resetting back to the beginning if we get too
 *    close to the end.
 */
static void
of_context_wait(struct pipe_context *pctx)
{
	struct of_context *ctx = of_context(pctx);
	uint32_t ts = of_ringbuffer_timestamp(ctx->ring);

	DBG("wait: %u", ts);

	of_pipe_wait(ctx->screen->pipe, ts);
	of_ringbuffer_reset(ctx->ring);
	of_ringmarker_mark(ctx->draw_start);
}

/* emit accumulated render cmds, needed for example if render target has
 * changed, or for flush()
 */
void
of_context_render(struct pipe_context *pctx)
{
	struct of_context *ctx = of_context(pctx);
	struct pipe_framebuffer_state *pfb = &ctx->framebuffer;

	DBG("needs_flush: %d", ctx->needs_flush);

	if (!ctx->needs_flush)
		return;

	/* TODO: Emit render commands here. */

	DBG("%p/%p/%p", ctx->ring->start, ctx->ring->cur, ctx->ring->end);

	/* if size in dwords is more than half the buffer size, then wait and
	 * wrap around:
	 */
	if ((ctx->ring->cur - ctx->ring->start) > ctx->ring->size/8)
		of_context_wait(pctx);

	ctx->needs_flush = false;
	ctx->cleared = ctx->restore = ctx->resolve = 0;
	ctx->num_draws = 0;

	of_resource(pfb->cbufs[0]->texture)->dirty = false;
	if (pfb->zsbuf)
		of_resource(pfb->zsbuf->texture)->dirty = false;
}

static void
of_context_flush(struct pipe_context *pctx, struct pipe_fence_handle **fence,
		unsigned flags)
{
	DBG("fence=%p", fence);

#if 0
	if (fence) {
		of_fence_ref(ctx->screen->fence.current,
				(struct of_fence **)fence);
	}
#endif

	of_context_render(pctx);
}

void
of_context_destroy(struct pipe_context *pctx)
{
	struct of_context *ctx = of_context(pctx);

	DBG("");

	if (ctx->blitter)
		util_blitter_destroy(ctx->blitter);

	of_ringmarker_del(ctx->draw_start);
	of_ringmarker_del(ctx->draw_end);
	of_ringbuffer_del(ctx->ring);

	FREE(ctx);
}

struct pipe_context *
of_context_init(struct of_context *ctx,
		struct pipe_screen *pscreen, void *priv)
{
	struct of_screen *screen = of_screen(pscreen);
	struct pipe_context *pctx;

	ctx->screen = screen;

	/* need some sane default in case state tracker doesn't
	 * set some state:
	 */
	ctx->sample_mask = 0xffff;

	pctx = &ctx->base;
	pctx->screen = pscreen;
	pctx->priv = priv;
	pctx->flush = of_context_flush;

	ctx->ring = of_ringbuffer_new(screen->pipe, 0x100000);
	if (!ctx->ring)
		goto fail;

	ctx->draw_start = of_ringmarker_new(ctx->ring);
	ctx->draw_end = of_ringmarker_new(ctx->ring);

	util_slab_create(&ctx->transfer_pool, sizeof(struct pipe_transfer),
			16, UTIL_SLAB_SINGLETHREADED);

	of_draw_init(pctx);
	of_resource_context_init(pctx);
	of_texture_init(pctx);
	of_state_init(pctx);

	ctx->blitter = util_blitter_create(pctx);
	if (!ctx->blitter)
		goto fail;


	return pctx;

fail:
	pctx->destroy(pctx);
	return NULL;
}
