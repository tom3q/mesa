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

#ifndef OPENFIMG_TEXTURE_H_
#define OPENFIMG_TEXTURE_H_

#include "openfimg_context.h"
#include "pipe/p_context.h"
#include "fimg_3dse.xml.h"

struct of_sampler_stateobj {
	struct pipe_sampler_state base;
	uint32_t vtx_tsta;
	uint32_t tsta;
};

static INLINE struct of_sampler_stateobj *
of_sampler_stateobj(struct pipe_sampler_state *samp)
{
	return (struct of_sampler_stateobj *)samp;
}

struct of_pipe_sampler_view {
	struct pipe_sampler_view base;
	struct of_resource *tex_resource;
	uint32_t vtx_tsta;
	uint32_t tsta;
	uint32_t width;
	uint32_t height;
};

static INLINE struct of_pipe_sampler_view *
of_pipe_sampler_view(struct pipe_sampler_view *pview)
{
	return (struct of_pipe_sampler_view *)pview;
}

unsigned of_get_const_idx(struct of_context *ctx,
		struct of_texture_stateobj *tex, unsigned samp_id);

void of_texture_init(struct pipe_context *pctx);

#endif /* OPENFIMG_TEXTURE_H_ */
