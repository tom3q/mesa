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
#include "util/u_clear.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_prim.h"
#include "util/u_format.h"
#include "util/u_surface.h"

#include "openfimg_draw.h"
#include "openfimg_emit.h"
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
	case PIPE_PRIM_LINE_LOOP:	return PTYPE_LINE_LOOP;
	case PIPE_PRIM_LINE_STRIP:	return PTYPE_LINE_STRIP;
	case PIPE_PRIM_TRIANGLES:	return PTYPE_TRIANGLES;
	case PIPE_PRIM_TRIANGLE_STRIP:	return PTYPE_TRIANGLE_STRIP;
	case PIPE_PRIM_TRIANGLE_FAN:	return PTYPE_TRIANGLE_FAN;
	}
	DBG("unsupported mode: (%s) %d", u_prim_name(mode), mode);
	assert(0);
	return 0;
}

static void
emit_vertexbufs(struct of_context *ctx)
{
	struct of_vertex_stateobj *vtx = ctx->vtx;
	struct of_vertexbuf_stateobj *vertexbuf = &ctx->vertexbuf;
	struct of_vertex_buf bufs[PIPE_MAX_ATTRIBS];
	unsigned i;

	if (!vtx->num_elements)
		return;

	for (i = 0; i < vtx->num_elements; i++) {
		struct pipe_vertex_element *elem = &vtx->pipe[i];
		struct pipe_vertex_buffer *vb =
				&vertexbuf->vb[elem->vertex_buffer_index];
		bufs[i].offset = vb->buffer_offset;
		bufs[i].size = of_bo_size(of_resource(vb->buffer)->bo);
		bufs[i].prsc = vb->buffer;
	}

	// NOTE I believe the 0x78 (or 0x9c in solid_vp) relates to the
	// CONST(20,0) (or CONST(26,0) in soliv_vp)

	of_emit_vertex_bufs(ctx->ring, 0x78, bufs, vtx->num_elements);
}

static void
of_draw(struct of_context *ctx, const struct pipe_draw_info *info)
{
	struct of_ringbuffer *ring = ctx->ring;

	of_emit_state(ctx, ctx->dirty);

	// TODO: Emit parameters from pipe_draw_info
	// TODO: Prepare geometry data
	// TODO: Emit draw commands
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

/*
 * TODO: implement all the three clears properly
 */
static void
of_clear(struct pipe_context *pctx, unsigned buffers,
		const union pipe_color_union *color, double depth, unsigned stencil)
{
	struct of_context *ctx = of_context(pctx);
	struct pipe_framebuffer_state *pfb = &ctx->framebuffer;

	util_clear(pctx, pfb, buffers, color, depth, stencil);

#warning TODO
}

static void
of_clear_render_target(struct pipe_context *pctx, struct pipe_surface *ps,
		const union pipe_color_union *color,
		unsigned x, unsigned y, unsigned w, unsigned h)
{
	DBG("TODO: x=%u, y=%u, w=%u, h=%u", x, y, w, h);

	util_clear_render_target(pctx, ps, color, x, y, w, h);

#warning TODO
}

static void
of_clear_depth_stencil(struct pipe_context *pctx, struct pipe_surface *ps,
		unsigned buffers, double depth, unsigned stencil,
		unsigned x, unsigned y, unsigned w, unsigned h)
{
	DBG("TODO: buffers=%u, depth=%f, stencil=%u, x=%u, y=%u, w=%u, h=%u",
			buffers, depth, stencil, x, y, w, h);

	util_clear_depth_stencil(pctx, ps, buffers, depth, stencil, x, y, w, h);

#warning TODO
}

void
of_draw_init(struct pipe_context *pctx)
{
	pctx->draw_vbo = of_draw_vbo;
	pctx->clear = of_clear;
	pctx->clear_render_target = of_clear_render_target;
	pctx->clear_depth_stencil = of_clear_depth_stencil;
}
