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

#ifndef OPENFIMG_STATE_H_
#define OPENFIMG_STATE_H_

#include "pipe/p_context.h"
#include "openfimg_context.h"

struct of_cso {
	unsigned ref_cnt;
	void *ptr;

	void (*release)(struct of_context *, void *);
};

struct of_blend_stateobj {
	struct pipe_blend_state base;
	struct of_cso cso;
	uint32_t fgpf_blend;
	uint32_t fgpf_logop;
	uint32_t fgpf_cbmsk;
	uint32_t fgpf_fbctl;
};

struct of_rasterizer_stateobj {
	struct pipe_rasterizer_state base;
	struct of_cso cso;
	uint32_t fgra_bfcull;
	uint32_t fgra_psize_min;
	uint32_t fgra_psize_max;
	uint32_t fgpe_vertex_context;
};

struct of_zsa_stateobj {
	struct pipe_depth_stencil_alpha_state base;
	struct of_cso cso;
	uint32_t fgpf_alphat;
	uint32_t fgpf_frontst;
	uint32_t fgpf_backst;
	uint32_t fgpf_deptht;
	uint32_t fgpf_dbmsk;
};

#define OF_STATIC_CSO(_cso)					\
	.cso = {						\
		.ptr = _cso,					\
		.ref_cnt = 1,					\
		.release = of_cso_dummy_release,		\
	}

#define OF_CSO_INIT(_cso, _release)				\
	do {							\
		(_cso)->cso.ptr = (_cso);			\
		(_cso)->cso.ref_cnt = 1;			\
		(_cso)->cso.release = _release;			\
	} while (0)

static INLINE void
OF_CSO_GET(struct of_cso *cso)
{
	++cso->ref_cnt;
}

static INLINE void
OF_CSO_PUT(struct of_context *ctx, struct of_cso *cso)
{
	if (!(--cso->ref_cnt)) {
		if (cso->release)
			cso->release(ctx, cso->ptr);
		else
			FREE(cso->ptr);
	}
}

#define OF_CSO_BIND(ctx, name, dirty_flag, hwcso)		\
	do {							\
		if (!hwcso)					\
			hwcso = &of_cso_dummy_ ## name;		\
								\
		ctx->cso.name = hwcso;				\
								\
		if (ctx->cso_active.name != hwcso)		\
			ctx->dirty |= dirty_flag;		\
		else						\
			ctx->dirty &= ~dirty_flag;		\
	} while (0)

#define OF_CSO_SET_ACTIVE(ctx, name)				\
	do {							\
		OF_CSO_GET(&ctx->cso.name->cso);		\
		OF_CSO_PUT(ctx, &ctx->cso_active.name->cso);		\
		ctx->cso_active.name = ctx->cso.name;		\
	} while (0)

#define OF_CSO_CLEAR(ctx, name)					\
	do {							\
		OF_CSO_GET(&of_cso_dummy_ ## name.cso);	\
		OF_CSO_PUT(ctx, &ctx->cso_active.name->cso);		\
		ctx->cso_active.name = &of_cso_dummy_ ## name;	\
	} while (0)

static inline bool of_depth_enabled(struct of_context *ctx)
{
	return ctx->cso.zsa && ctx->cso.zsa->base.depth.enabled;
}

static inline bool of_stencil_enabled(struct of_context *ctx)
{
	return ctx->cso.zsa && ctx->cso.zsa->base.stencil[0].enabled;
}

static inline bool of_logicop_enabled(struct of_context *ctx)
{
	return ctx->cso.blend && ctx->cso.blend->base.logicop_enable;
}

static inline bool of_blend_enabled(struct of_context *ctx, unsigned n)
{
	return ctx->cso.blend && ctx->cso.blend->base.rt[n].blend_enable;
}

static INLINE struct pipe_scissor_state *
of_context_get_scissor(struct of_context *ctx)
{
	if (ctx->cso.rasterizer && ctx->cso.rasterizer->base.scissor)
		return &ctx->scissor;
	return &ctx->disabled_scissor;
}

static INLINE struct of_blend_stateobj *
of_blend_stateobj(struct pipe_blend_state *blend)
{
	return (struct of_blend_stateobj *)blend;
}

static INLINE struct of_rasterizer_stateobj *
of_rasterizer_stateobj(struct pipe_rasterizer_state *rast)
{
	return (struct of_rasterizer_stateobj *)rast;
}

static INLINE struct of_zsa_stateobj *
of_zsa_stateobj(struct pipe_depth_stencil_alpha_state *zsa)
{
	return (struct of_zsa_stateobj *)zsa;
}

void of_state_init(struct pipe_context *pctx);
void of_cso_dummy_release(struct of_context *, void *cso);

extern struct of_blend_stateobj of_cso_dummy_blend;
extern struct of_rasterizer_stateobj of_cso_dummy_rasterizer;
extern struct of_zsa_stateobj of_cso_dummy_zsa;
extern struct of_vertex_stateobj of_cso_dummy_vtx;

#endif /* OPENFIMG_STATE_H_ */
