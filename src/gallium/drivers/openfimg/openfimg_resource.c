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

#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_transfer.h"
#include "util/u_string.h"
#include "util/u_surface.h"

#include "openfimg_resource.h"
#include "openfimg_screen.h"
#include "openfimg_surface.h"
#include "openfimg_context.h"
#include "openfimg_util.h"

#include <errno.h>

static void
realloc_bo(struct of_resource *rsc, uint32_t size)
{
	struct of_screen *screen = of_screen(rsc->base.b.screen);
	uint32_t flags = DRM_FREEDRENO_GEM_CACHE_WCOMBINE |
			DRM_FREEDRENO_GEM_TYPE_KMEM; /* TODO */

	if (rsc->bo)
		fd_bo_del(rsc->bo);

	rsc->bo = fd_bo_new(screen->dev, size, flags);
	rsc->timestamp = 0;
	rsc->dirty = false;
}

static void of_resource_transfer_flush_region(struct pipe_context *pctx,
		struct pipe_transfer *ptrans,
		const struct pipe_box *box)
{
	struct of_context *ctx = of_context(pctx);
	struct of_resource *rsc = of_resource(ptrans->resource);

	if (rsc->dirty)
		of_context_render(pctx);

	if (rsc->timestamp) {
		fd_pipe_wait(ctx->pipe, rsc->timestamp);
		rsc->timestamp = 0;
	}
}

static void
of_resource_transfer_unmap(struct pipe_context *pctx,
		struct pipe_transfer *ptrans)
{
	struct of_context *ctx = of_context(pctx);
	struct of_resource *rsc = of_resource(ptrans->resource);
	if (!(ptrans->usage & PIPE_TRANSFER_UNSYNCHRONIZED))
		fd_bo_cpu_fini(rsc->bo);
	pipe_resource_reference(&ptrans->resource, NULL);
	util_slab_free(&ctx->transfer_pool, ptrans);
}

static void *
of_resource_transfer_map(struct pipe_context *pctx,
		struct pipe_resource *prsc,
		unsigned level, unsigned usage,
		const struct pipe_box *box,
		struct pipe_transfer **pptrans)
{
	struct of_context *ctx = of_context(pctx);
	struct of_resource *rsc = of_resource(prsc);
	struct of_resource_slice *slice = of_resource_slice(rsc, level);
	struct pipe_transfer *ptrans;
	enum pipe_format format = prsc->format;
	uint32_t op = 0;
	char *buf;

	ptrans = util_slab_alloc(&ctx->transfer_pool);
	if (!ptrans)
		return NULL;

	/* util_slab_alloc() doesn't zero: */
	memset(ptrans, 0, sizeof(*ptrans));

	pipe_resource_reference(&ptrans->resource, prsc);
	ptrans->level = level;
	ptrans->usage = usage;
	ptrans->box = *box;
	ptrans->stride = slice->pitch * rsc->cpp;
	ptrans->layer_stride = ptrans->stride;

	if (usage & PIPE_TRANSFER_READ)
		op |= DRM_FREEDRENO_PREP_READ;

	if (usage & PIPE_TRANSFER_WRITE)
		op |= DRM_FREEDRENO_PREP_WRITE;

	if (usage & PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE)
		op |= DRM_FREEDRENO_PREP_NOSYNC;

	/* some state trackers (at least XA) don't do this.. */
	if (!(usage & (PIPE_TRANSFER_FLUSH_EXPLICIT | PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE)))
		of_resource_transfer_flush_region(pctx, ptrans, box);

	if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) {
		int ret;

		ret = fd_bo_cpu_prep(rsc->bo, ctx->pipe, op);
		if ((ret == -EBUSY)
		    && (usage & PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE)) {
			realloc_bo(rsc, fd_bo_size(rsc->bo));
		} else if (ret) {
			of_resource_transfer_unmap(pctx, ptrans);
			return NULL;
		}
	}

	buf = fd_bo_map(rsc->bo);
	if (!buf) {
		of_resource_transfer_unmap(pctx, ptrans);
		return NULL;
	}

	*pptrans = ptrans;

	return buf + slice->offset +
		box->y / util_format_get_blockheight(format) * ptrans->stride +
		box->x / util_format_get_blockwidth(format) * rsc->cpp +
		box->z * slice->size0;
}

static void
of_resource_destroy(struct pipe_screen *pscreen,
		struct pipe_resource *prsc)
{
	struct of_resource *rsc = of_resource(prsc);
	if (rsc->bo)
		fd_bo_del(rsc->bo);
	FREE(rsc);
}

static boolean
of_resource_get_handle(struct pipe_screen *pscreen,
		struct pipe_resource *prsc,
		struct winsys_handle *handle)
{
	struct of_resource *rsc = of_resource(prsc);

	return of_screen_bo_get_handle(pscreen, rsc->bo,
			rsc->slices[0].pitch * rsc->cpp, handle);
}


static const struct u_resource_vtbl of_resource_vtbl = {
		.resource_get_handle      = of_resource_get_handle,
		.resource_destroy         = of_resource_destroy,
		.transfer_map             = of_resource_transfer_map,
		.transfer_flush_region    = of_resource_transfer_flush_region,
		.transfer_unmap           = of_resource_transfer_unmap,
		.transfer_inline_write    = u_default_transfer_inline_write,
};

static uint32_t
setup_slices(struct of_resource *rsc)
{
	struct pipe_resource *prsc = &rsc->base.b;
	uint32_t level, size = 0, pixels = 0;
	uint32_t width = prsc->width0;
	uint32_t height = prsc->height0;
	uint32_t depth = prsc->depth0;

	for (level = 0; level <= prsc->last_level; level++) {
		struct of_resource_slice *slice = of_resource_slice(rsc, level);
		unsigned cur_pixels;

		slice->pitch = width;
		slice->offset = size;
		slice->pixoffset = pixels;
		slice->size0 = slice->pitch * height * rsc->cpp;

		cur_pixels = ROUND_UP(width * height * depth
					* prsc->array_size, 16);

		pixels += cur_pixels;
		size += cur_pixels * rsc->cpp;

		width = u_minify(width, 1);
		height = u_minify(height, 1);
		depth = u_minify(depth, 1);
	}

	return size;
}

/**
 * Create a new texture object, using the given template info.
 */
static struct pipe_resource *
of_resource_create(struct pipe_screen *pscreen,
		const struct pipe_resource *tmpl)
{
	struct of_resource *rsc = CALLOC_STRUCT(of_resource);
	struct pipe_resource *prsc = &rsc->base.b;
	uint32_t size;

	VDBG("target=%d, format=%s, %ux%ux%u, array_size=%u, last_level=%u, "
			"nr_samples=%u, usage=%u, bind=%x, flags=%x",
			tmpl->target, util_format_name(tmpl->format),
			tmpl->width0, tmpl->height0, tmpl->depth0,
			tmpl->array_size, tmpl->last_level, tmpl->nr_samples,
			tmpl->usage, tmpl->bind, tmpl->flags);

	if (!rsc)
		return NULL;

	*prsc = *tmpl;

	pipe_reference_init(&prsc->reference, 1);
	prsc->screen = pscreen;

	rsc->base.vtbl = &of_resource_vtbl;
	rsc->cpp = util_format_get_blocksize(tmpl->format);

	assert(rsc->cpp);

	size = setup_slices(rsc);

	realloc_bo(rsc, size);
	if (!rsc->bo)
		goto fail;

	return prsc;
fail:
	of_resource_destroy(pscreen, prsc);
	return NULL;
}

/**
 * Create a texture from a winsys_handle. The handle is often created in
 * another process by first creating a pipe texture and then calling
 * resource_get_handle.
 */
static struct pipe_resource *
of_resource_from_handle(struct pipe_screen *pscreen,
		const struct pipe_resource *tmpl,
		struct winsys_handle *handle)
{
	struct of_resource *rsc = CALLOC_STRUCT(of_resource);
	struct of_resource_slice *slice = &rsc->slices[0];
	struct pipe_resource *prsc = &rsc->base.b;

	VDBG("target=%d, format=%s, %ux%ux%u, array_size=%u, last_level=%u, "
			"nr_samples=%u, usage=%u, bind=%x, flags=%x",
			tmpl->target, util_format_name(tmpl->format),
			tmpl->width0, tmpl->height0, tmpl->depth0,
			tmpl->array_size, tmpl->last_level, tmpl->nr_samples,
			tmpl->usage, tmpl->bind, tmpl->flags);

	if (!rsc)
		return NULL;

	*prsc = *tmpl;

	pipe_reference_init(&prsc->reference, 1);
	prsc->screen = pscreen;

	rsc->bo = of_screen_bo_from_handle(pscreen, handle, &slice->pitch);
	if (!rsc->bo)
		goto fail;

	rsc->base.vtbl = &of_resource_vtbl;
	rsc->cpp = util_format_get_blocksize(tmpl->format);
	slice->pitch /= rsc->cpp;

	assert(rsc->cpp);

	return prsc;

fail:
	of_resource_destroy(pscreen, prsc);
	return NULL;
}

static void
save_blitter_state(struct of_context *ctx)
{
	util_blitter_save_vertex_buffer_slot(ctx->blitter, ctx->vertexbuf.vb);
	util_blitter_save_vertex_elements(ctx->blitter, ctx->cso.vtx);
	util_blitter_save_vertex_shader(ctx->blitter, ctx->cso.vp);
	util_blitter_save_rasterizer(ctx->blitter, ctx->cso.rasterizer);
	util_blitter_save_viewport(ctx->blitter, &ctx->viewport);
	util_blitter_save_scissor(ctx->blitter, &ctx->scissor);
	util_blitter_save_fragment_shader(ctx->blitter, ctx->cso.fp);
	util_blitter_save_blend(ctx->blitter, ctx->cso.blend);
	util_blitter_save_depth_stencil_alpha(ctx->blitter, ctx->cso.zsa);
	util_blitter_save_stencil_ref(ctx->blitter, &ctx->stencil_ref);
	util_blitter_save_sample_mask(ctx->blitter, ctx->sample_mask);
	util_blitter_save_framebuffer(ctx->blitter, &ctx->framebuffer.base);
	util_blitter_save_fragment_sampler_states(ctx->blitter,
			ctx->fragtex.num_samplers,
			(void **)ctx->fragtex.samplers);
	util_blitter_save_fragment_sampler_views(ctx->blitter,
			ctx->fragtex.num_textures, ctx->fragtex.textures);
}

/**
 * Copy a block of pixels from one resource to another.
 * The resource must be of the same format.
 * Resources with nr_samples > 1 are not allowed.
 */
static void
of_resource_copy_region(struct pipe_context *pctx,
		struct pipe_resource *dst,
		unsigned dst_level,
		unsigned dstx, unsigned dsty, unsigned dstz,
		struct pipe_resource *src,
		unsigned src_level,
		const struct pipe_box *src_box)
{
	struct of_context *ctx = of_context(pctx);

	if (dst->target == PIPE_BUFFER || src->target == PIPE_BUFFER)
		goto sw_fallback;

	if (!util_blitter_is_copy_supported(ctx->blitter, dst, src))
		goto sw_fallback;

	save_blitter_state(ctx);
	util_blitter_copy_texture(ctx->blitter,
			dst, dst_level, dstx, dsty, dstz,
			src, src_level, src_box);
	return;

sw_fallback:
	util_resource_copy_region(pctx, dst, dst_level, dstx, dsty, dstz,
					src, src_level, src_box);
}

/* Optimal hardware path for blitting pixels.
 * Scaling, format conversion, up- and downsampling (resolve) are allowed.
 */
static void
of_blit(struct pipe_context *pctx, const struct pipe_blit_info *blit_info)
{
	struct of_context *ctx = of_context(pctx);
	struct pipe_blit_info info = *blit_info;

	if (info.src.resource->nr_samples > 1 &&
			info.dst.resource->nr_samples <= 1 &&
			!util_format_is_depth_or_stencil(info.src.resource->format) &&
			!util_format_is_pure_integer(info.src.resource->format)) {
		DBG("color resolve unimplemented");
		return;
	}

	if (util_try_blit_via_copy_region(pctx, &info)) {
		return; /* done */
	}

	if (info.mask & PIPE_MASK_S) {
		DBG("cannot blit stencil, skipping");
		info.mask &= ~PIPE_MASK_S;
	}

	if (!util_blitter_is_blit_supported(ctx->blitter, &info)) {
		DBG("blit unsupported %s -> %s",
				util_format_short_name(info.src.resource->format),
				util_format_short_name(info.dst.resource->format));
		return;
	}

	save_blitter_state(ctx);
	util_blitter_blit(ctx->blitter, &info);
}

static void
of_flush_resource(struct pipe_context *ctx, struct pipe_resource *resource)
{
}

void
of_resource_screen_init(struct pipe_screen *pscreen)
{
	pscreen->resource_create = of_resource_create;
	pscreen->resource_from_handle = of_resource_from_handle;
	pscreen->resource_get_handle = u_resource_get_handle_vtbl;
	pscreen->resource_destroy = u_resource_destroy_vtbl;
}

void
of_resource_context_init(struct pipe_context *pctx)
{
	pctx->transfer_map = u_transfer_map_vtbl;
	pctx->transfer_flush_region = u_transfer_flush_region_vtbl;
	pctx->transfer_unmap = u_transfer_unmap_vtbl;
	pctx->transfer_inline_write = u_transfer_inline_write_vtbl;
	pctx->create_surface = of_create_surface;
	pctx->surface_destroy = of_surface_destroy;
	pctx->resource_copy_region = of_resource_copy_region;
	pctx->blit = of_blit;
	pctx->flush_resource = of_flush_resource;
}
