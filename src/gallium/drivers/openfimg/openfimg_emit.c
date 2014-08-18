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
#include "util/u_helpers.h"

#include "openfimg_resource.h"
#include "openfimg_emit.h"
#include "openfimg_context.h"
#include "openfimg_texture.h"
#include "openfimg_util.h"
#include "openfimg_state.h"

#include "compiler/openfimg_program.h"

/*
 * Utility functions
 */

static void
emit_constants(struct fd_ringbuffer *ring,
	       struct of_constbuf_stateobj *constbuf, bool emit_immediates,
	       struct of_shader_stateobj *shader)
{
	unsigned enabled_mask = constbuf->enabled_mask;
	uint32_t base = 0;
	uint32_t *pkt;
	unsigned i;

	// XXX TODO only emit dirty consts.. but we need to keep track if
	// they are clobbered by a clear, gmem2mem, or mem2gmem..
	constbuf->dirty_mask = enabled_mask;

	/* emit user constants: */
	while (enabled_mask) {
		unsigned index = u_bit_scan(&enabled_mask);
		struct pipe_constant_buffer *cb = &constbuf->cb[index];
		unsigned size = align(cb->buffer_size, 4) / 4; /* size in dwords */

		/* I expect that size should be a multiple of vec4's: */
		assert(size == align(size, 4));

		/* gallium could leave const buffers bound above what the
		 * current shader uses.. don't let that confuse us.
		 */
		if (base >= (shader->first_immediate * 4))
			break;

		if (constbuf->dirty_mask & (1 << index)) {
			const uint32_t *dwords;

			/* and even if the start of the const buffer is before
			 * first_immediate, the end may not be:
			 */
			if (base + size >= 4 * shader->first_immediate)
				size = (4 * shader->first_immediate) - base;

			if (cb->user_buffer) {
				dwords = cb->user_buffer;
			} else {
				struct of_resource *rsc = of_resource(cb->buffer);
				dwords = fd_bo_map(rsc->bo);
			}

			dwords = (uint32_t *)(((uint8_t *)dwords) + cb->buffer_offset);

			if (size) {
				pkt = OUT_PKT(ring, G3D_REQUEST_SHADER_DATA);
				OUT_RING(ring, RSD_UNIT_TYPE_OFFS(shader->type,
						G3D_SHADER_DATA_FLOAT, base));
				for (i = 0; i < size; i++)
					OUT_RING(ring, *(dwords++));
				END_PKT(ring, pkt);
			}

			constbuf->dirty_mask &= ~(1 << index);
		}

		base += size;
	}

	/* emit shader immediates: */
	if (!emit_immediates || !shader->num_immediates)
		return;

	pkt = OUT_PKT(ring, G3D_REQUEST_SHADER_DATA);
	OUT_RING(ring, RSD_UNIT_TYPE_OFFS(shader->type,
			G3D_SHADER_DATA_FLOAT, 4 * shader->first_immediate));

	for (i = 0; i < shader->num_immediates; i++)
		OUT_RING(ring, shader->immediates[i]);

	END_PKT(ring, pkt);
}

typedef uint32_t texmask;

static void
emit_texture(struct fd_ringbuffer *ring, struct of_context *ctx,
	     struct of_texture_stateobj *tex, unsigned samp_id)
{
	struct of_sampler_stateobj *sampler;
	struct of_pipe_sampler_view *view;
	uint32_t *pkt;

	sampler = of_sampler_stateobj(tex->samplers[samp_id]);
	view = of_pipe_sampler_view(tex->textures[samp_id]);

	of_reference_draw_buffer(ctx, &view->tex_resource->base.b);

	pkt = OUT_PKT(ring, G3D_REQUEST_TEXTURE);
	OUT_RING(ring, sampler->tsta | view->tsta);
	OUT_RING(ring, view->width);
	OUT_RING(ring, view->height);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);
	OUT_RING(ring, view->base.u.tex.first_level);
	OUT_RING(ring, view->base.u.tex.last_level);
	OUT_RING(ring, 0);
	OUT_RING(ring, fd_bo_handle(view->tex_resource->bo));
	OUT_RING(ring, (samp_id << 24));
	END_PKT(ring, pkt);
}

static void
emit_vtx_texture(struct fd_ringbuffer *ring, struct of_context *ctx,
		 struct of_texture_stateobj *tex, unsigned samp_id)
{
	struct of_sampler_stateobj *sampler;
	struct of_pipe_sampler_view *view;
	uint32_t *pkt;

	sampler = of_sampler_stateobj(tex->samplers[samp_id]);
	view = of_pipe_sampler_view(tex->textures[samp_id]);

	of_reference_draw_buffer(ctx, &view->tex_resource->base.b);

	pkt = OUT_PKT(ring, G3D_REQUEST_VTX_TEXTURE);
	OUT_RING(ring, sampler->vtx_tsta | view->vtx_tsta);
	OUT_RING(ring, 0);
	OUT_RING(ring, fd_bo_handle(view->tex_resource->bo));
	OUT_RING(ring, (samp_id << 24));
	END_PKT(ring, pkt);
}

static void
emit_textures(struct fd_ringbuffer *ring, struct of_context *ctx)
{
	unsigned i;

	for (i = 0; i < ctx->fragtex.num_samplers; i++)
		if (ctx->fragtex.samplers[i])
			emit_texture(ring, ctx, &ctx->fragtex, i);
}

static void
emit_vtx_textures(struct fd_ringbuffer *ring, struct of_context *ctx)
{
	unsigned i;

	for (i = 0; i < ctx->verttex.num_samplers; i++)
		if (ctx->verttex.samplers[i])
			emit_vtx_texture(ring, ctx, &ctx->verttex, i);
}

void
of_emit_state(struct of_context *ctx, uint32_t dirty)
{
	struct fd_ringbuffer *ring = ctx->ring;
	uint32_t *pkt;

	if (!dirty)
		goto done;

	/* NOTE: we probably want to eventually refactor this so each state
	 * object handles emitting it's own state..  although the mapping of
	 * state to registers is not always orthogonal, sometimes a single
	 * register contains bitfields coming from multiple state objects,
	 * so not sure the best way to deal with that yet.
	 */

	if (dirty & OF_DIRTY_FRAMEBUFFER) {
		struct of_framebuffer_stateobj *fb = &ctx->framebuffer;
		struct of_resource *rsc;

		if (fb->base.cbufs[0]) {
			of_reference_draw_buffer(ctx,
						fb->base.cbufs[0]->texture);
			rsc = of_resource(fb->base.cbufs[0]->texture);
		} else {
			rsc = NULL;
		}

		pkt = OUT_PKT(ring, G3D_REQUEST_COLORBUFFER);
		OUT_RING(ring, fb->fgpf_fbctl);
		OUT_RING(ring, 0);
		OUT_RING(ring, fb->base.width);
		OUT_RING(ring, rsc ? fd_bo_handle(rsc->bo) : 0);
		OUT_RING(ring, rsc ? 0 : G3D_CBUFFER_DETACH);
		END_PKT(ring, pkt);

		if (fb->base.zsbuf) {
			of_reference_draw_buffer(ctx, fb->base.zsbuf->texture);
			rsc = of_resource(fb->base.zsbuf->texture);
		} else {
			rsc = NULL;
		}

		pkt = OUT_PKT(ring, G3D_REQUEST_DEPTHBUFFER);
		OUT_RING(ring, 0);
		OUT_RING(ring, rsc ? fd_bo_handle(rsc->bo) : 0);
		OUT_RING(ring, rsc ? 0 : G3D_DBUFFER_DETACH);
		END_PKT(ring, pkt);
	}

	if (dirty & (OF_DIRTY_PROG_VP | OF_DIRTY_VTXSTATE))
		of_program_emit(ctx, ctx->cso.vp);

	if (dirty & OF_DIRTY_PROG_FP)
		of_program_emit(ctx, ctx->cso.fp);

	if (dirty & (OF_DIRTY_PROG_VP | OF_DIRTY_CONSTBUF)) {
		emit_constants(ring, &ctx->constbuf[PIPE_SHADER_VERTEX],
				dirty & OF_DIRTY_PROG_VP, ctx->cso.vp);
		ctx->cso_active.vp = ctx->cso.vp;
	}

	if (dirty & (OF_DIRTY_PROG_FP | OF_DIRTY_CONSTBUF)) {
		emit_constants(ring, &ctx->constbuf[PIPE_SHADER_FRAGMENT],
				dirty & OF_DIRTY_PROG_FP, ctx->cso.fp);
		ctx->cso_active.fp = ctx->cso.fp;
	}

	if (dirty & OF_DIRTY_VERTTEX)
		emit_vtx_textures(ring, ctx);

	if (dirty & OF_DIRTY_FRAGTEX)
		emit_textures(ring, ctx);

	pkt = OUT_PKT(ring, G3D_REQUEST_REGISTER_WRITE);

	if (dirty & OF_DIRTY_RASTERIZER) {
		struct of_rasterizer_stateobj *rasterizer =
				of_rasterizer_stateobj(ctx->cso.rasterizer);

		OUT_RING(ring, REG_FGRA_D_OFF_EN);
		OUT_RING(ring, ctx->cso.rasterizer->offset_tri);
		OUT_RING(ring, REG_FGRA_D_OFF_FACTOR);
		OUT_RING(ring, fui(ctx->cso.rasterizer->offset_scale));
		OUT_RING(ring, REG_FGRA_D_OFF_UNITS);
		OUT_RING(ring, fui(ctx->cso.rasterizer->offset_units));
		OUT_RING(ring, REG_FGRA_BFCULL);
		OUT_RING(ring, rasterizer->fgra_bfcull);
		OUT_RING(ring, REG_FGRA_PWIDTH);
		OUT_RING(ring, fui(ctx->cso.rasterizer->point_size));
		OUT_RING(ring, REG_FGRA_PSIZE_MIN);
		OUT_RING(ring, rasterizer->fgra_psize_min);
		OUT_RING(ring, REG_FGRA_PSIZE_MAX);
		OUT_RING(ring, rasterizer->fgra_psize_max);
		OUT_RING(ring, REG_FGRA_LWIDTH);
		OUT_RING(ring, fui(ctx->cso.rasterizer->line_width));

		ctx->cso_active.rasterizer = ctx->cso.rasterizer;
	}

	if (dirty & (OF_DIRTY_SCISSOR | OF_DIRTY_RASTERIZER)) {
		struct pipe_scissor_state *scissor =
				of_context_get_scissor(ctx);

		OUT_RING(ring, REG_FGRA_XCLIP);
		OUT_RING(ring, FGRA_XCLIP_MAX_VAL(scissor->maxx)
				| FGRA_XCLIP_MIN_VAL(scissor->minx));
		OUT_RING(ring, REG_FGRA_YCLIP);
		OUT_RING(ring, FGRA_YCLIP_MAX_VAL(scissor->maxy)
				| FGRA_YCLIP_MIN_VAL(scissor->miny));
	}

	if (dirty & OF_DIRTY_VIEWPORT) {
		struct pipe_viewport_state *viewport = &ctx->viewport;

		OUT_RING(ring, REG_FGPE_VIEWPORT_OX);
		OUT_RING(ring, fui(viewport->translate[0]));
		OUT_RING(ring, REG_FGPE_VIEWPORT_OY);
		OUT_RING(ring, fui(viewport->translate[1]));
		OUT_RING(ring, REG_FGPE_DEPTHRANGE_HALF_F_ADD_N);
		OUT_RING(ring, fui(viewport->translate[2]));
		OUT_RING(ring, REG_FGPE_VIEWPORT_HALF_PX);
		OUT_RING(ring, fui(viewport->scale[0]));
		OUT_RING(ring, REG_FGPE_VIEWPORT_HALF_PY);
		OUT_RING(ring, fui(viewport->scale[1]));
		OUT_RING(ring, REG_FGPE_DEPTHRANGE_HALF_F_SUB_N);
		OUT_RING(ring, fui(viewport->scale[2]));
	}

	if (dirty & OF_DIRTY_BLEND) {
		struct of_blend_stateobj *blend =
				of_blend_stateobj(ctx->cso.blend);

		OUT_RING(ring, REG_FGPF_BLEND);
		OUT_RING(ring, blend->fgpf_blend);
		OUT_RING(ring, REG_FGPF_LOGOP);
		OUT_RING(ring, blend->fgpf_logop);
		OUT_RING(ring, REG_FGPF_CBMSK);
		OUT_RING(ring, blend->fgpf_cbmsk);
		OUT_RING(ring, REG_FGPF_FBCTL);
		OUT_RING(ring, blend->fgpf_fbctl);

		ctx->cso_active.blend = ctx->cso.blend;
	}

	if (dirty & OF_DIRTY_BLEND_COLOR) {
		OUT_RING(ring, REG_FGPF_CCLR);
		OUT_RING(ring, ctx->blend_color);
	}

	if (dirty & (OF_DIRTY_ZSA | OF_DIRTY_STENCIL_REF)) {
		struct of_zsa_stateobj *zsa = of_zsa_stateobj(ctx->cso.zsa);
		struct pipe_stencil_ref *sr = &ctx->stencil_ref;

		OUT_RING(ring, REG_FGPF_FRONTST);
		OUT_RING(ring, zsa->fgpf_frontst |
				FGPF_FRONTST_VALUE(sr->ref_value[0]));
		OUT_RING(ring, REG_FGPF_BACKST);
		OUT_RING(ring, zsa->fgpf_backst |
				FGPF_BACKST_VALUE(sr->ref_value[1]));

		ctx->cso_active.zsa = ctx->cso.zsa;
	}

	if (dirty & OF_DIRTY_ZSA) {
		struct of_zsa_stateobj *zsa = of_zsa_stateobj(ctx->cso.zsa);

		OUT_RING(ring, REG_FGPF_ALPHAT);
		OUT_RING(ring, zsa->fgpf_alphat);
		OUT_RING(ring, REG_FGPF_DEPTHT);
		OUT_RING(ring, zsa->fgpf_deptht);
		OUT_RING(ring, REG_FGPF_DBMSK);
		OUT_RING(ring, zsa->fgpf_dbmsk);

		ctx->cso_active.zsa = ctx->cso.zsa;
	}

	END_PKT(ring, pkt);

done:
	ctx->dirty &= ~dirty;
}

/* emit per-context initialization:
 */
void
of_emit_setup_solid(struct of_context *ctx)
{

}

void
of_emit_setup_blit(struct of_context *ctx)
{
	DBG("TODO");
}

void
of_emit_setup(struct of_context *ctx)
{
	/* Mark all context parts dirty */
	ctx->dirty = (OF_DIRTY_SCISSOR << 1) - 1;
}
