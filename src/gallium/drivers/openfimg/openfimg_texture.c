/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
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
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "pipe/p_state.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"

#include "openfimg_texture.h"
#include "openfimg_resource.h"
#include "openfimg_context.h"
#include "openfimg_util.h"

static enum fgtu_addr_mode
tex_clamp(unsigned wrap)
{
	switch (wrap) {
	case PIPE_TEX_WRAP_REPEAT:
		return ADDR_MODE_REPEAT;
	case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
		return ADDR_MODE_CLAMP_TO_EDGE;
	case PIPE_TEX_WRAP_MIRROR_REPEAT:
		return ADDR_MODE_FLIP;
	default:
		DBG("invalid wrap: %u", wrap);
		return 0;
	}
}

static uint32_t
tex_filter(unsigned filter)
{
	switch (filter) {
	case PIPE_TEX_FILTER_NEAREST:
		return 0;
	case PIPE_TEX_FILTER_LINEAR:
		return TSTA_MAG_FILTER;
	default:
		DBG("invalid filter: %u", filter);
		return 0;
	}
}

static void
of_sampler_state_delete(struct pipe_context *pctx, void *hwcso)
{
	FREE(hwcso);
}

static void
of_sampler_view_destroy(struct pipe_context *pctx,
		struct pipe_sampler_view *view)
{
	pipe_resource_reference(&view->texture, NULL);
	FREE(view);
}

static void bind_sampler_states(struct of_texture_stateobj *prog,
		unsigned nr, void **hwcso)
{
	unsigned i;
	unsigned new_nr = 0;

	for (i = 0; i < nr; i++) {
		if (hwcso[i])
			new_nr++;
		prog->samplers[i] = hwcso[i];
		prog->dirty_samplers |= (1 << i);
	}

	for (; i < prog->num_samplers; i++) {
		prog->samplers[i] = NULL;
		prog->dirty_samplers |= (1 << i);
	}

	prog->num_samplers = new_nr;
}

static void set_sampler_views(struct of_texture_stateobj *prog,
		unsigned nr, struct pipe_sampler_view **views)
{
	unsigned i;
	unsigned new_nr = 0;

	for (i = 0; i < nr; i++) {
		if (views[i])
			new_nr++;
		pipe_sampler_view_reference(&prog->textures[i], views[i]);
		prog->dirty_samplers |= (1 << i);
	}

	for (; i < prog->num_textures; i++) {
		pipe_sampler_view_reference(&prog->textures[i], NULL);
		prog->dirty_samplers |= (1 << i);
	}

	prog->num_textures = new_nr;
}
static void
of_sampler_states_bind(struct pipe_context *pctx,
		unsigned shader, unsigned start,
		unsigned nr, void **hwcso)
{
	struct of_context *ctx = of_context(pctx);

	assert(start == 0);

	if (shader == PIPE_SHADER_FRAGMENT) {
		/* on a2xx, since there is a flat address space for textures/samplers,
		 * a change in # of fragment textures/samplers will trigger patching and
		 * re-emitting the vertex shader:
		 */
		if (nr != ctx->fragtex.num_samplers)
			ctx->dirty |= OF_DIRTY_TEXSTATE;

		bind_sampler_states(&ctx->fragtex, nr, hwcso);
		ctx->dirty |= OF_DIRTY_FRAGTEX;
	}
	else if (shader == PIPE_SHADER_VERTEX) {
		bind_sampler_states(&ctx->verttex, nr, hwcso);
		ctx->dirty |= OF_DIRTY_VERTTEX;
	}
}


static void
of_fragtex_set_sampler_views(struct pipe_context *pctx, unsigned nr,
		struct pipe_sampler_view **views)
{
	struct of_context *ctx = of_context(pctx);

	/* on a2xx, since there is a flat address space for textures/samplers,
	 * a change in # of fragment textures/samplers will trigger patching and
	 * re-emitting the vertex shader:
	 */
	if (nr != ctx->fragtex.num_textures)
		ctx->dirty |= OF_DIRTY_TEXSTATE;

	set_sampler_views(&ctx->fragtex, nr, views);
	ctx->dirty |= OF_DIRTY_FRAGTEX;
}

static void
of_verttex_set_sampler_views(struct pipe_context *pctx, unsigned nr,
		struct pipe_sampler_view **views)
{
	struct of_context *ctx = of_context(pctx);
	set_sampler_views(&ctx->verttex, nr, views);
	ctx->dirty |= OF_DIRTY_VERTTEX;
}

static void
of_set_sampler_views(struct pipe_context *pctx, unsigned shader,
                     unsigned start, unsigned nr,
                     struct pipe_sampler_view **views)
{
   assert(start == 0);
   switch (shader) {
   case PIPE_SHADER_FRAGMENT:
      of_fragtex_set_sampler_views(pctx, nr, views);
      break;
   case PIPE_SHADER_VERTEX:
      of_verttex_set_sampler_views(pctx, nr, views);
      break;
   default:
      ;
   }
}

static void *
of_sampler_state_create(struct pipe_context *pctx,
		const struct pipe_sampler_state *cso)
{
	struct of_sampler_stateobj *so = CALLOC_STRUCT(of_sampler_stateobj);

	if (!so)
		return NULL;

	so->base = *cso;

#warning TODO

	return so;
}

static struct pipe_sampler_view *
of_sampler_view_create(struct pipe_context *pctx, struct pipe_resource *prsc,
		const struct pipe_sampler_view *cso)
{
	struct of_pipe_sampler_view *so = CALLOC_STRUCT(of_pipe_sampler_view);
	struct of_resource *rsc = of_resource(prsc);

	if (!so)
		return NULL;

	so->base = *cso;
	pipe_reference(NULL, &prsc->reference);
	so->base.texture = prsc;
	so->base.reference.count = 1;
	so->base.context = pctx;

	so->tex_resource =  rsc;
	so->fmt = of_pipe2texture(cso->format);

#warning TODO

	return &so->base;
}

/* map gallium sampler-id to hw const-idx.. adreno uses a flat address
 * space of samplers (const-idx), so we need to map the gallium sampler-id
 * which is per-shader to a global const-idx space.
 *
 * Fragment shader sampler maps directly to const-idx, and vertex shader
 * is offset by the # of fragment shader samplers.  If the # of fragment
 * shader samplers changes, this shifts the vertex shader indexes.
 *
 * TODO maybe we can do frag shader 0..N  and vert shader N..0 to avoid
 * this??
 */
unsigned
of_get_const_idx(struct of_context *ctx, struct of_texture_stateobj *tex,
		unsigned samp_id)
{
	if (tex == &ctx->fragtex)
		return samp_id;
	return samp_id + ctx->fragtex.num_samplers;
}

void
of_texture_init(struct pipe_context *pctx)
{
	pctx->create_sampler_state = of_sampler_state_create;
	pctx->delete_sampler_state = of_sampler_state_delete;

	pctx->create_sampler_view = of_sampler_view_create;
	pctx->sampler_view_destroy = of_sampler_view_destroy;

	pctx->bind_sampler_states = of_sampler_states_bind;
	pctx->set_sampler_views = of_set_sampler_views;
}
