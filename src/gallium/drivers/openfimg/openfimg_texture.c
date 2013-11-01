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
#include "openfimg_context.h"
#include "openfimg_util.h"

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

void
of_texture_init(struct pipe_context *pctx)
{
	pctx->delete_sampler_state = of_sampler_state_delete;

	pctx->sampler_view_destroy = of_sampler_view_destroy;

	pctx->bind_sampler_states = of_sampler_states_bind;
	pctx->set_sampler_views = of_set_sampler_views;
}
