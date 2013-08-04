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

#include "openfimg_surface.h"
#include "openfimg_resource.h"
#include "openfimg_util.h"

#include "util/u_memory.h"
#include "util/u_inlines.h"

struct pipe_surface *
of_create_surface(struct pipe_context *pctx,
		struct pipe_resource *ptex,
		const struct pipe_surface *surf_tmpl)
{
//	struct of_resource* tex = of_resource(ptex);
	struct of_surface* surface = CALLOC_STRUCT(of_surface);

	assert(surf_tmpl->u.tex.first_layer == surf_tmpl->u.tex.last_layer);

	if (surface) {
		struct pipe_surface *psurf = &surface->base;
		unsigned level = surf_tmpl->u.tex.level;

		pipe_reference_init(&psurf->reference, 1);
		pipe_resource_reference(&psurf->texture, ptex);

		psurf->context = pctx;
		psurf->format = surf_tmpl->format;
		psurf->width = u_minify(ptex->width0, level);
		psurf->height = u_minify(ptex->height0, level);
		psurf->u.tex.level = level;
		psurf->u.tex.first_layer = surf_tmpl->u.tex.first_layer;
		psurf->u.tex.last_layer = surf_tmpl->u.tex.last_layer;

		// TODO
		DBG("TODO: %ux%u", psurf->width, psurf->height);
	}

	return &surface->base;
}

void
of_surface_destroy(struct pipe_context *pctx, struct pipe_surface *psurf)
{
	pipe_resource_reference(&psurf->texture, NULL);
	FREE(psurf);
}
