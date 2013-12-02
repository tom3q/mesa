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
#include "openfimg_program.h"
#include "openfimg_texture.h"
#include "openfimg_util.h"
#include "openfimg_state.h"

enum g3d_shader_type {
	G3D_SHADER_VERTEX,
	G3D_SHADER_PIXEL,

	G3D_NUM_SHADERS
};

enum g3d_shader_data_type {
	G3D_SHADER_DATA_FLOAT,
	G3D_SHADER_DATA_INT,
	G3D_SHADER_DATA_BOOL,

	G3D_NUM_SHADER_DATA_TYPES
};

/*
 * Utility functions
 */

static inline uint32_t RSP_UNIT_NATTRIB(uint8_t unit, uint8_t nattrib)
{
	return (unit << 8) | nattrib;
}

static inline uint32_t RSP_DCOUNT(uint16_t type1, uint16_t type2)
{
	return (type2 << 16) | type1;
}

static inline uint32_t RSD_UNIT_TYPE_OFFS(uint8_t unit, uint8_t type,
					  uint16_t offs)
{
	return (unit << 24) | (type << 16) | offs;
}

static void
emit_constants(struct of_ringbuffer *ring,
	       struct of_constbuf_stateobj *constbuf, bool emit_immediates,
	       struct of_shader_stateobj *shader)
{
	uint32_t enabled_mask = constbuf->enabled_mask;
	uint32_t base = 0;
	unsigned i;

	// XXX TODO only emit dirty consts.. but we need to keep track if
	// they are clobbered by a clear, gmem2mem, or mem2gmem..
	constbuf->dirty_mask = enabled_mask;

	/* emit user constants: */
	while (enabled_mask) {
		unsigned index = ffs(enabled_mask) - 1;
		struct pipe_constant_buffer *cb = &constbuf->cb[index];
		unsigned size = align(cb->buffer_size, 4) / 4; /* size in dwords */

		/* hmm, sometimes we still seem to end up with consts bound,
		 * even if shader isn't using them, which ends up overwriting
		 * const reg's used for immediates.. this is a hack to work
		 * around that:
		 */
		if (base >= (shader->first_immediate * 4))
			break;

		if (constbuf->dirty_mask & (1 << index)) {
			const uint32_t *dwords;

			if (cb->user_buffer) {
				dwords = cb->user_buffer;
			} else {
				struct of_resource *rsc = of_resource(cb->buffer);
				dwords = of_bo_map(rsc->bo);
			}

			dwords = (uint32_t *)(((uint8_t *)dwords) + cb->buffer_offset);

			OUT_PKT(ring, G3D_REQUEST_SHADER_DATA, size + 1);
			OUT_RING(ring, RSD_UNIT_TYPE_OFFS(shader->type,
					G3D_SHADER_DATA_FLOAT, base));
			for (i = 0; i < size; i++)
				OUT_RING(ring, *(dwords++));

			constbuf->dirty_mask &= ~(1 << index);
		}

		base += size;
		enabled_mask &= ~(1 << index);
	}

	/* emit shader immediates: */
	if (!emit_immediates)
		return;

	OUT_PKT(ring, G3D_REQUEST_SHADER_DATA,
			1 + 4 * shader->num_immediates);
	OUT_RING(ring, RSD_UNIT_TYPE_OFFS(shader->type,
			G3D_SHADER_DATA_FLOAT, 4 * shader->first_immediate));

	for (i = 0; i < shader->num_immediates; i++) {
		OUT_RING(ring, shader->immediates[i].val[0]);
		OUT_RING(ring, shader->immediates[i].val[1]);
		OUT_RING(ring, shader->immediates[i].val[2]);
		OUT_RING(ring, shader->immediates[i].val[3]);
	}
}

typedef uint32_t texmask;

static texmask
emit_texture(struct of_ringbuffer *ring, struct of_context *ctx,
		struct of_texture_stateobj *tex, unsigned samp_id, texmask emitted)
{
	unsigned const_idx = of_get_const_idx(ctx, tex, samp_id);
	struct of_sampler_stateobj *sampler;
	struct of_pipe_sampler_view *view;

	if (emitted & (1 << const_idx))
		return 0;

	sampler = of_sampler_stateobj(tex->samplers[samp_id]);
	view = of_pipe_sampler_view(tex->textures[samp_id]);

#warning TODO

	return (1 << const_idx);
}

static void
emit_textures(struct of_ringbuffer *ring, struct of_context *ctx)
{
	texmask emitted = 0;
	unsigned i;

	for (i = 0; i < ctx->verttex.num_samplers; i++)
		if (ctx->verttex.samplers[i])
			emitted |= emit_texture(ring, ctx, &ctx->verttex, i, emitted);

	for (i = 0; i < ctx->fragtex.num_samplers; i++)
		if (ctx->fragtex.samplers[i])
			emitted |= emit_texture(ring, ctx, &ctx->fragtex, i, emitted);
}

void
of_emit_state(struct of_context *ctx, uint32_t dirty)
{
	struct of_ringbuffer *ring = ctx->ring;

	/* NOTE: we probably want to eventually refactor this so each state
	 * object handles emitting it's own state..  although the mapping of
	 * state to registers is not always orthogonal, sometimes a single
	 * register contains bitfields coming from multiple state objects,
	 * so not sure the best way to deal with that yet.
	 */

	if (dirty & OF_DIRTY_FRAMEBUFFER) {
#warning TODO
	}

	if (dirty & OF_DIRTY_RASTERIZER) {
		struct of_rasterizer_stateobj *rasterizer =
				of_rasterizer_stateobj(ctx->rasterizer);

		OUT_PKT(ring, G3D_REQUEST_REGISTER_WRITE, 2 * 6);
		OUT_RING(ring, REG_FGRA_D_OFF_EN);
		OUT_RING(ring, ctx->rasterizer->offset_tri);
		OUT_RING(ring, REG_FGRA_D_OFF_FACTOR);
		OUT_RING(ring, fui(ctx->rasterizer->offset_scale));
		OUT_RING(ring, REG_FGRA_D_OFF_UNITS);
		OUT_RING(ring, fui(ctx->rasterizer->offset_units));
		OUT_RING(ring, REG_FGRA_BFCULL);
		OUT_RING(ring, rasterizer->fgra_bfcull);
		OUT_RING(ring, REG_FGRA_PWIDTH);
		OUT_RING(ring, fui(ctx->rasterizer->point_size));
		OUT_RING(ring, REG_FGRA_PSIZE_MIN);
		OUT_RING(ring, rasterizer->fgra_psize_min);
		OUT_RING(ring, REG_FGRA_PSIZE_MAX);
		OUT_RING(ring, rasterizer->fgra_psize_max);
		OUT_RING(ring, REG_FGRA_LWIDTH);
		OUT_RING(ring, fui(ctx->rasterizer->line_width));
	}

	if (dirty & OF_DIRTY_SCISSOR) {
		struct pipe_scissor_state *scissor =
				of_context_get_scissor(ctx);

		OUT_PKT(ring, G3D_REQUEST_REGISTER_WRITE, 2 * 2);
		OUT_RING(ring, REG_FGRA_XCLIP);
		OUT_RING(ring, (scissor->maxx << 16) | scissor->minx);
		OUT_RING(ring, REG_FGRA_YCLIP);
		OUT_RING(ring, (scissor->maxy << 16) | scissor->miny);
	}

	if (dirty & OF_DIRTY_VIEWPORT) {
		struct pipe_viewport_state *viewport = &ctx->viewport;

		OUT_PKT(ring, G3D_REQUEST_REGISTER_WRITE, 2 * 6);
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

	if (dirty & (OF_DIRTY_PROG | OF_DIRTY_VTXSTATE | OF_DIRTY_TEXSTATE)) {
		of_program_validate(ctx);
		of_program_emit(ring, &ctx->prog);
	}

	if (dirty & (OF_DIRTY_PROG | OF_DIRTY_CONSTBUF)) {
		emit_constants(ring, &ctx->constbuf[PIPE_SHADER_VERTEX],
				dirty & OF_DIRTY_PROG, ctx->prog.vp);
		emit_constants(ring, &ctx->constbuf[PIPE_SHADER_FRAGMENT],
				dirty & OF_DIRTY_PROG, ctx->prog.fp);
	}

	if (dirty & OF_DIRTY_BLEND) {
		struct of_blend_stateobj *blend =
				of_blend_stateobj(ctx->blend);

		OUT_PKT(ring, G3D_REQUEST_REGISTER_WRITE, 2 * 4);
		OUT_RING(ring, REG_FGPF_BLEND);
		OUT_RING(ring, blend->fgpf_blend);
		OUT_RING(ring, REG_FGPF_LOGOP);
		OUT_RING(ring, blend->fgpf_logop);
		OUT_RING(ring, REG_FGPF_CBMSK);
		OUT_RING(ring, blend->fgpf_cbmsk);
		OUT_RING(ring, REG_FGPF_FBCTL);
		OUT_RING(ring, blend->fgpf_fbctl);
	}

	if (dirty & OF_DIRTY_BLEND_COLOR) {
		OUT_PKT(ring, G3D_REQUEST_REGISTER_WRITE, 2 * 1);
		OUT_RING(ring, REG_FGPF_CCLR);
		OUT_RING(ring, ctx->blend_color);
	}

	if (dirty & (OF_DIRTY_ZSA | OF_DIRTY_STENCIL_REF)) {
		struct of_zsa_stateobj *zsa = of_zsa_stateobj(ctx->zsa);
		struct pipe_stencil_ref *sr = &ctx->stencil_ref;

		OUT_PKT(ring, G3D_REQUEST_REGISTER_WRITE, 2 * 2);
		OUT_RING(ring, REG_FGPF_FRONTST);
		OUT_RING(ring, zsa->fgpf_frontst |
				FGPF_FRONTST_VALUE(sr->ref_value[0]));
		OUT_RING(ring, REG_FGPF_BACKST);
		OUT_RING(ring, zsa->fgpf_backst |
				FGPF_BACKST_VALUE(sr->ref_value[1]));
	}

	if (dirty & OF_DIRTY_ZSA) {
		struct of_zsa_stateobj *zsa = of_zsa_stateobj(ctx->zsa);

		OUT_PKT(ring, G3D_REQUEST_REGISTER_WRITE, 2 * 3);
		OUT_RING(ring, REG_FGPF_ALPHAT);
		OUT_RING(ring, zsa->fgpf_alphat);
		OUT_RING(ring, REG_FGPF_DEPTHT);
		OUT_RING(ring, zsa->fgpf_deptht);
		OUT_RING(ring, REG_FGPF_DBMSK);
		OUT_RING(ring, zsa->fgpf_dbmsk);
	}

	if (dirty & (OF_DIRTY_VERTTEX | OF_DIRTY_FRAGTEX | OF_DIRTY_PROG))
		emit_textures(ring, ctx);

	ctx->dirty &= ~dirty;
}

/* emit per-context initialization:
 */
void
of_emit_setup(struct of_context *ctx)
{
	struct of_ringbuffer *ring = ctx->ring;

#warning TODO

	of_ringbuffer_flush(ring);
	of_ringmarker_mark(ctx->draw_start);
}
