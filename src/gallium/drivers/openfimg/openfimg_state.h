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

#ifndef FREEDRENO_STATE_H_
#define FREEDRENO_STATE_H_

#include "pipe/p_context.h"
#include "openfimg_context.h"

static inline bool of_depth_enabled(struct of_context *ctx)
{
	return ctx->zsa && ctx->zsa->depth.enabled;
}

static inline bool of_stencil_enabled(struct of_context *ctx)
{
	return ctx->zsa && ctx->zsa->stencil[0].enabled;
}

static inline bool of_logicop_enabled(struct of_context *ctx)
{
	return ctx->blend && ctx->blend->logicop_enable;
}

static inline bool of_blend_enabled(struct of_context *ctx, unsigned n)
{
	return ctx->blend && ctx->blend->rt[n].blend_enable;
}

void of_state_init(struct pipe_context *pctx);

#endif /* FREEDRENO_STATE_H_ */
