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

#include "pipe/p_state.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_prim.h"
#include "util/u_format.h"

#include "openfimg_draw.h"
#include "openfimg_context.h"
#include "openfimg_state.h"
#include "openfimg_resource.h"
#include "openfimg_util.h"


static enum fgpe_vctx_ptype
mode2primtype(unsigned mode)
{
	switch (mode) {
	case PIPE_PRIM_POINTS:		return PTYPE_POINTS;
	case PIPE_PRIM_LINES:		return PTYPE_LINES;
	case PIPE_PRIM_LINE_STRIP:	return PTYPE_LINE_STRIP;
	case PIPE_PRIM_TRIANGLES:	return PTYPE_TRIANGLES;
	case PIPE_PRIM_TRIANGLE_STRIP:	return PTYPE_TRIANGLE_STRIP;
	case PIPE_PRIM_TRIANGLE_FAN:	return PTYPE_TRIANGLE_FAN;
	}
	DBG("unsupported mode: (%s) %d", u_prim_name(mode), mode);
	assert(0);
	return 0;
}

/* this is same for a2xx/a3xx, so split into helper: */
void
of_draw_emit(struct of_context *ctx, const struct pipe_draw_info *info)
{
	struct of_ringbuffer *ring = ctx->ring;
	struct pipe_index_buffer *idx = &ctx->indexbuf;
	struct of_bo *idx_bo = NULL;
	uint32_t idx_width, idx_size, idx_offset;

	if (info->indexed) {
		/* TODO: Handle user buffers? */
		assert(!idx->user_buffer);

		idx_bo = of_resource(idx->buffer)->bo;
		idx_width = idx->index_size;
		idx_size = idx->index_size * info->count;
		idx_offset = idx->offset;
	} else {
		idx_bo = NULL;
		idx_width = 0;
		idx_size = 0;
		idx_offset = 0;
	}

	/* TODO: Prepare and emit draw batches here. */
}

static void
of_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info)
{
	struct of_context *ctx = of_context(pctx);
	struct pipe_framebuffer_state *pfb = &ctx->framebuffer;
	struct pipe_scissor_state *scissor = of_context_get_scissor(ctx);
	unsigned i, buffers = 0;

	/* if we supported transform feedback, we'd have to disable this: */
	if (((scissor->maxx - scissor->minx) *
			(scissor->maxy - scissor->miny)) == 0) {
		return;
	}

	ctx->needs_flush = true;

	/*
	 * Figure out the buffers/features we need:
	 */

	if (of_depth_enabled(ctx)) {
		buffers |= OF_BUFFER_DEPTH;
		of_resource(pfb->zsbuf->texture)->dirty = true;
	}

	if (of_stencil_enabled(ctx)) {
		buffers |= OF_BUFFER_STENCIL;
		of_resource(pfb->zsbuf->texture)->dirty = true;
	}

	for (i = 0; i < pfb->nr_cbufs; i++) {
		struct pipe_resource *surf = pfb->cbufs[i]->texture;

		of_resource(surf)->dirty = true;
		buffers |= OF_BUFFER_COLOR;
	}

	ctx->num_draws++;

	/* any buffers that haven't been cleared, we need to restore: */
	ctx->restore |= buffers & (OF_BUFFER_ALL & ~ctx->cleared);
	/* and any buffers used, need to be resolved: */
	ctx->resolve |= buffers;

	/* TODO: Implement draw here. */
}

/* TODO figure out how to make better use of existing state mechanism
 * for clear (and possibly gmem->mem / mem->gmem) so we can (a) keep
 * track of what state really actually changes, and (b) reduce the code
 * in the a2xx/a3xx parts.
 */

static void
of_clear(struct pipe_context *pctx, unsigned buffers,
		const union pipe_color_union *color, double depth, unsigned stencil)
{
	struct of_context *ctx = of_context(pctx);
	struct pipe_framebuffer_state *pfb = &ctx->framebuffer;

	ctx->cleared |= buffers;
	ctx->resolve |= buffers;
	ctx->needs_flush = true;

	if (buffers & PIPE_CLEAR_COLOR)
		of_resource(pfb->cbufs[0]->texture)->dirty = true;

	if (buffers & (PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL))
		of_resource(pfb->zsbuf->texture)->dirty = true;

	DBG("%x depth=%f, stencil=%u (%s/%s)", buffers, depth, stencil,
			util_format_name(pfb->cbufs[0]->format),
			pfb->zsbuf ? util_format_name(pfb->zsbuf->format) : "none");

	/* TODO: Implement clear here. */

	ctx->dirty |= OF_DIRTY_ZSA |
			OF_DIRTY_RASTERIZER |
			OF_DIRTY_SAMPLE_MASK |
			OF_DIRTY_PROG |
			OF_DIRTY_CONSTBUF |
			OF_DIRTY_BLEND;

	if (of_mesa_debug & OF_DBG_DCLEAR)
		ctx->dirty = 0xffffffff;
}

static void
of_clear_render_target(struct pipe_context *pctx, struct pipe_surface *ps,
		const union pipe_color_union *color,
		unsigned x, unsigned y, unsigned w, unsigned h)
{
	DBG("TODO: x=%u, y=%u, w=%u, h=%u", x, y, w, h);
}

static void
of_clear_depth_stencil(struct pipe_context *pctx, struct pipe_surface *ps,
		unsigned buffers, double depth, unsigned stencil,
		unsigned x, unsigned y, unsigned w, unsigned h)
{
	DBG("TODO: buffers=%u, depth=%f, stencil=%u, x=%u, y=%u, w=%u, h=%u",
			buffers, depth, stencil, x, y, w, h);
}

void
of_draw_init(struct pipe_context *pctx)
{
	pctx->draw_vbo = of_draw_vbo;
	pctx->clear = of_clear;
	pctx->clear_render_target = of_clear_render_target;
	pctx->clear_depth_stencil = of_clear_depth_stencil;
}
