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

#include "cso_cache/cso_cache.h"
#include "cso_cache/cso_hash.h"

#include "openfimg_draw.h"
#include "openfimg_emit.h"
#include "openfimg_context.h"
#include "openfimg_state.h"
#include "openfimg_resource.h"
#include "openfimg_vertex.h"
#include "openfimg_util.h"

#include <stdlib.h>

static INLINE unsigned
of_draw_hash(struct of_draw_info *req)
{
	unsigned hash_key;
	hash_key = cso_construct_key(req, sizeof(*req));
	return hash_key;
}

static void
of_allocate_vertex_buffer(struct of_context *ctx, struct of_vertex_info *vertex)
{
	struct of_vertex_transfer *transfer;
	unsigned batch_size;
	unsigned size;
	unsigned sum;
	unsigned i;

	transfer = vertex->transfers;
	sum = 0;

	for (i = 0; i < vertex->num_transfers; ++i, ++transfer)
		sum += ROUND_UP(transfer->width, 4);

	batch_size = VERTEX_BUFFER_SIZE / sum;

	for (; batch_size > 0; --batch_size) {
		size = 0;
		for (i = 0; i < vertex->num_transfers; ++i, ++transfer) {
			transfer->offset = size;
			size += ROUND_UP(batch_size
					* ROUND_UP(transfer->width, 4), 32);
		}
		if (size <= VERTEX_BUFFER_SIZE)
			break;
	}

	vertex->batch_size = batch_size;
}

static void
of_primconvert_run(struct of_context *ctx, struct of_vertex_info *vertex)
{
	struct pipe_transfer *src_transfer = NULL, *dst_transfer = NULL;
	const struct of_draw_info *draw = &vertex->key;
	struct pipe_index_buffer *new_ib = &vertex->ib;
	const struct pipe_index_buffer *ib = &draw->ib;
	const struct pipe_draw_info *info = &draw->info;
	const void *src;
	void *dst;

	new_ib->buffer = pipe_buffer_create(ctx->base.screen,
						PIPE_BIND_INDEX_BUFFER,
						PIPE_USAGE_IMMUTABLE,
						new_ib->index_size
						* vertex->count);

	dst = pipe_buffer_map(&ctx->base, new_ib->buffer, PIPE_TRANSFER_WRITE,
				&dst_transfer);

	if (info->indexed) {
		src = ib->user_buffer;
		if (!src) {
			src = pipe_buffer_map(&ctx->base, ib->buffer,
						PIPE_TRANSFER_READ,
						&src_transfer);
		}
		vertex->trans_func(src, info->start, vertex->count, dst);
	}
	else {
		vertex->gen_func(info->start, vertex->count, dst);
	}

	if (src_transfer)
		pipe_buffer_unmap(&ctx->base, src_transfer);

	if (dst_transfer)
		pipe_buffer_unmap(&ctx->base, dst_transfer);
}

static void
of_primconvert_release(struct of_context *ctx, struct of_vertex_info *vertex)
{
	struct pipe_index_buffer *new_ib = &vertex->ib;

	pipe_resource_reference(&new_ib->buffer, NULL);
}

static unsigned int dummy_const;

static void
of_build_vertex_data(struct of_context *ctx, struct of_vertex_info *vertex)
{
	struct pipe_transfer *vb_transfer[OF_MAX_ATTRIBS];
	const struct pipe_index_buffer *ib = &vertex->ib;
	const struct of_draw_info *draw = &vertex->key;
	struct pipe_transfer *ib_transfer = NULL;
	struct of_vertex_transfer *transfer;
	const void *vb_ptr[OF_MAX_ATTRIBS];
	bool primconvert = false;
	const void *indices;
	unsigned i;

	if (!of_supported_prim(ctx, draw->info.mode)) {
		of_primconvert_run(ctx, vertex);
		primconvert = true;
	}

	memset(vb_ptr, 0, sizeof(vb_ptr));
	memset(vb_transfer, 0, sizeof(vb_transfer));

	transfer = vertex->transfers;
	for (i = 0; i < vertex->num_transfers; ++i, ++transfer) {
		unsigned buf_idx = transfer->vertex_buffer_index;
		const struct pipe_vertex_buffer *vb = &draw->vb[buf_idx];

		if (!vb_ptr[buf_idx]) {
			if (vb->user_buffer)
				vb_ptr[buf_idx] = vb->user_buffer;
			else
				vb_ptr[buf_idx] = pipe_buffer_map(&ctx->base,
							vb->buffer,
							PIPE_TRANSFER_READ,
							&vb_transfer[buf_idx]);
		}

		transfer->pointer = vb_ptr[buf_idx] + transfer->src_offset;
	}

	if (!vertex->indexed) {
		of_prepare_draw_seq(ctx, vertex);
	} else {
		if (ib->buffer)
			indices = pipe_buffer_map(&ctx->base, ib->buffer,
							PIPE_TRANSFER_READ,
							&ib_transfer);
		else
			indices = ib->user_buffer;

		switch (ib->index_size) {
		case 4:
			of_prepare_draw_idx32(ctx, vertex, indices);
			break;
		case 2:
			of_prepare_draw_idx16(ctx, vertex, indices);
			break;
		case 1:
			of_prepare_draw_idx8(ctx, vertex, indices);
			break;
		default:
			assert(0);
		}

		if (ib_transfer)
			pipe_buffer_unmap(&ctx->base, ib_transfer);
	}

#warning TODO
	// TODO: Prepare constant elements
	vertex->const_data = &dummy_const;
	vertex->const_size = 0;

	if (primconvert)
		of_primconvert_release(ctx, vertex);

	transfer = vertex->transfers;
	for (i = 0; i < vertex->num_transfers; ++i, ++transfer) {
		unsigned buf_idx = transfer->vertex_buffer_index;

		if (!vb_ptr[buf_idx])
			continue;

		if (vb_transfer[buf_idx])
			pipe_buffer_unmap(&ctx->base, vb_transfer[buf_idx]);

		vb_ptr[buf_idx] = NULL;
	}
}

static int
array_compare(const void *a, const void *b, void *data)
{
	struct of_vertex_info *vertex = data;
	struct of_draw_info *draw = &vertex->key;
	unsigned elem1 = *(const unsigned *)a;
	unsigned elem2 = *(const unsigned *)b;

	if (draw->elements[elem1].vertex_buffer_index
	    != draw->elements[elem2].vertex_buffer_index)
		return draw->elements[elem1].vertex_buffer_index
				- draw->elements[elem2].vertex_buffer_index;

	return draw->elements[elem1].src_offset
					- draw->elements[elem2].src_offset;
}

static void
of_primconvert_prepare(struct of_context *ctx, struct of_vertex_info *vertex)
{
	const struct pipe_rasterizer_state *rast = ctx->rasterizer;
	const struct of_draw_info *draw = &vertex->key;
	struct pipe_index_buffer *new_ib = &vertex->ib;
	const struct pipe_draw_info *info = &draw->info;
	unsigned api_pv;

	memset(new_ib, 0, sizeof(*new_ib));
	vertex->indexed = true;

	api_pv = (rast->flatshade
			&& !rast->flatshade_first) ? PV_LAST : PV_FIRST;

	if (info->indexed)
		u_index_translator(ctx->primtype_mask,
					info->mode, draw->ib.index_size,
					info->count, api_pv, api_pv,
					&vertex->mode, &new_ib->index_size,
					&vertex->count, &vertex->trans_func);
	else
		u_index_generator(ctx->primtype_mask,
					info->mode, info->start, info->count,
					api_pv, api_pv, &vertex->mode,
					&new_ib->index_size, &vertex->count,
					&vertex->gen_func);
}

static void
of_vtx_format(struct of_vertex_element *elem, enum pipe_format fmt)
{
	const struct util_format_description *desc;
	int first_comp;
	enum fghi_attrib_dt type;

	desc = util_format_description(fmt);
	if (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
		goto out_unknown;

	first_comp = util_format_get_first_non_void_channel(fmt);
	if (first_comp < 0)
		goto out_unknown;

	if (desc->is_mixed)
		goto out_unknown;

	switch (desc->channel[first_comp].type) {
	case UTIL_FORMAT_TYPE_FLOAT:
		switch (desc->channel[first_comp].size) {
		case 16:
			type = DT_HFLOAT;
			break;
		case 32:
			type = DT_FLOAT;
			break;
		default:
			goto out_unknown;
		}
		break;
	case UTIL_FORMAT_TYPE_FIXED:
		if (desc->channel[first_comp].size != 32)
			goto out_unknown;
		if (desc->channel[first_comp].normalized)
			type = DT_NFIXED;
		else
			type = DT_FIXED;
		break;
	case UTIL_FORMAT_TYPE_SIGNED:
		switch (desc->channel[first_comp].size) {
		case 8:
			type = DT_BYTE;
			break;
		case 16:
			type = DT_SHORT;
			break;
		case 32:
			type = DT_INT;
			break;
		default:
			goto out_unknown;
		}
		if (desc->channel[first_comp].normalized)
			type += DT_NBYTE;
		break;
	case UTIL_FORMAT_TYPE_UNSIGNED:
		switch (desc->channel[first_comp].size) {
		case 8:
			type = DT_UBYTE;
			break;
		case 16:
			type = DT_USHORT;
			break;
		case 32:
			type = DT_UINT;
			break;
		default:
			goto out_unknown;
		}
		if (desc->channel[first_comp].normalized)
			type += DT_NBYTE;
		break;
	default:
		goto out_unknown;
	}

	elem->attrib = FGHI_ATTRIB_DT(type) |
			FGHI_ATTRIB_NUM_COMP(desc->nr_channels - 1) |
			FGHI_ATTRIB_SRCX(0) | FGHI_ATTRIB_SRCY(1) |
			FGHI_ATTRIB_SRCZ(2) | FGHI_ATTRIB_SRCW(3);
	elem->width = desc->block.bits / 8;

	return;

out_unknown:
	DBG("unsupported vertex format %s\n", util_format_name(fmt));
}

static struct of_vertex_info *
of_create_vertex_info(struct of_context *ctx,
		      const struct of_draw_info *draw, bool bypass_cache)
{
	struct of_vertex_info *vertex = CALLOC_STRUCT(of_vertex_info);
	struct of_vertex_transfer *transfer;
	unsigned arrays[OF_MAX_ATTRIBS];
	unsigned num_arrays;
	unsigned i;

	if (vertex == NULL)
		return NULL;

	memcpy(&vertex->key, draw, sizeof(*draw));

	vertex->mode = ctx->primtypes[vertex->mode];
	vertex->bypass_cache = bypass_cache;
	vertex->first_draw = true;
	vertex->num_transfers = 0;

	/* emulate unsupported primitives: */
	if (of_supported_prim(ctx, draw->info.mode)) {
		vertex->indexed = draw->info.indexed;
		vertex->mode = draw->info.mode;
		vertex->count = draw->info.count;
		vertex->trans_func = NULL;
		vertex->gen_func = NULL;
		memcpy(&vertex->ib, &draw->ib, sizeof(vertex->ib));
	} else {
		of_primconvert_prepare(ctx, vertex);
	}

	num_arrays = 0;
	for (i = 0; i < draw->num_elements; ++i) {
		const struct pipe_vertex_element *draw_element =
							&draw->elements[i];
		struct of_vertex_element *vtx_element = &vertex->elements[i];
		const struct pipe_vertex_buffer *vb =
				&draw->vb[draw_element->vertex_buffer_index];

		of_vtx_format(vtx_element, draw_element->src_format);

		if (!vb->stride)
			continue;

		arrays[num_arrays++] = i;
	}

	/* Try to detect interleaved arrays */
	qsort_r(arrays, num_arrays, sizeof(*arrays), array_compare, vertex);

	transfer = vertex->transfers;
	for (i = 0; i < num_arrays;) {
		unsigned attrib = arrays[i];
		const struct pipe_vertex_element *pipe =
						&draw->elements[attrib];
		struct of_vertex_element *element = &vertex->elements[attrib];
		const struct pipe_vertex_buffer *buffer =
					&draw->vb[pipe->vertex_buffer_index];

		transfer->vertex_buffer_index = pipe->vertex_buffer_index;
		transfer->src_offset = pipe->src_offset;
		transfer->stride = buffer->stride;
		transfer->width = element->width;
		element->offset = 0;
		element->transfer_index = vertex->num_transfers;

		for (++i; i < num_arrays; ++i) {
			unsigned attrib2 = arrays[i];
			const struct pipe_vertex_element *pipe2 =
						&draw->elements[attrib2];
			struct of_vertex_element *element2
						= &vertex->elements[attrib2];
			unsigned offset = pipe2->src_offset - pipe->src_offset;

			/* Interleaved arrays reside in one vertex buffer. */
			if (pipe->vertex_buffer_index
			    != pipe2->vertex_buffer_index)
				break;

			/*
			 * Interleaved arrays must be contiguous
			 * and attributes must be word-aligned.
			 */
			if (offset != ROUND_UP(transfer->width, 4))
				break;

			transfer->width = offset + element2->width;
			element2->offset = offset;
			element2->transfer_index = vertex->num_transfers;
		}

		assert(transfer->width <= transfer->stride);
		++transfer;
		++vertex->num_transfers;
	}

	of_allocate_vertex_buffer(ctx, vertex);
	of_build_vertex_data(ctx, vertex);

	return vertex;
}

static void
of_emit_draw(struct of_context *ctx, struct of_vertex_info *info)
{
	const struct of_draw_info *draw = &info->key;
	struct of_rasterizer_stateobj *rasterizer;
	struct of_ringbuffer *ring = ctx->ring;
	struct of_vertex_buffer *buf, *tmp;
	unsigned i;

	rasterizer = of_rasterizer_stateobj(ctx->rasterizer);

	OUT_PKT(ring, G3D_REQUEST_REGISTER_WRITE,
		2 * (draw->num_elements * 3 + 2));

	for (i = 0; i < draw->num_elements; ++i) {
		struct of_vertex_element *element = &info->elements[i];
		struct of_vertex_transfer *transfer =
				&info->transfers[element->transfer_index];
		unsigned stride = ROUND_UP(transfer->width, 4);
		unsigned offset = transfer->offset + element->offset;

		OUT_RING(ring, REG_FGHI_ATTRIB(i));
		OUT_RING(ring, element->attrib);
		OUT_RING(ring, REG_FGHI_ATTRIB_VBCTRL(i));
		OUT_RING(ring, FGHI_ATTRIB_VBCTRL_STRIDE(stride)
				| FGHI_ATTRIB_VBCTRL_RANGE(0xffff));
		OUT_RING(ring, REG_FGHI_ATTRIB_VBBASE(i));
		OUT_RING(ring, FGHI_ATTRIB_VBBASE_ADDR(offset));
	}

	OUT_RING(ring, REG_FGPE_VERTEX_CONTEXT);
	OUT_RING(ring, rasterizer->fgpe_vertex_context |
			info->draw_mode | FGPE_VERTEX_CONTEXT_VSOUT(8));

	LIST_FOR_EACH_ENTRY_SAFE(buf, tmp, &info->buffers, list) {
		OUT_PKT(ring, G3D_REQUEST_DRAW, 4);
		OUT_RING(ring, buf->nr_vertices);
		OUT_RING(ring, of_bo_handle(of_resource(buf->buffer)->bo));
		OUT_RING(ring, 0);
		OUT_RING(ring, VERTEX_BUFFER_SIZE);

		if (info->first_draw || info->bypass_cache) {
			LIST_DEL(&buf->list);
			of_put_batch_buffer(ctx, buf);
		}
	}

	info->first_draw = false;
}

static void
of_kill_draw_caches(struct of_context *ctx, struct pipe_resource *buf)
{
#warning TODO
	assert(0);
}

static void
of_draw(struct of_context *ctx, const struct pipe_draw_info *info)
{
	struct of_vertexbuf_stateobj *vertexbuf = &ctx->vertexbuf;
	struct pipe_index_buffer *indexbuf = &ctx->indexbuf;
	struct of_vertex_stateobj *vtx = ctx->vtx;
	struct of_vertex_info *vertex = NULL;
	int buffer_map[PIPE_MAX_ATTRIBS];
	bool bypass_cache = false;
	struct of_draw_info draw;
	bool index_dirty = false;
	unsigned dirty = 0;
	unsigned hash_key;
	unsigned i;

	assert(vtx->num_elements > 0);

	memcpy(&draw.info, info, sizeof(draw.info));
	if (info->indexed) {
		memcpy(&draw.ib, indexbuf, sizeof(draw.ib));
		if (!indexbuf->buffer)
			bypass_cache = true;
		else if (of_resource(indexbuf->buffer)->dirty)
			index_dirty = true;
	}

	for (i = 0; i < PIPE_MAX_ATTRIBS; ++i)
		buffer_map[i] = -1;

	memcpy(draw.elements, vtx->pipe,
		vtx->num_elements * sizeof(draw.elements[0]));
	draw.num_elements = vtx->num_elements;

	draw.num_vb = 0;
	for (i = 0; i < vtx->num_elements; ++i) {
		struct pipe_vertex_element *elem = &vtx->pipe[i];
		struct pipe_vertex_buffer *vb =
				&vertexbuf->vb[elem->vertex_buffer_index];

		if (!vb->buffer)
			bypass_cache = true;
		else if (of_resource(vb->buffer)->dirty)
			dirty |= 1 << elem->vertex_buffer_index;

		if (buffer_map[elem->vertex_buffer_index] < 0) {
			memcpy(&draw.vb[draw.num_vb], vb, sizeof(draw.vb[0]));
			buffer_map[elem->vertex_buffer_index] = draw.num_vb;
			++draw.num_vb;
		}

		draw.elements[i].vertex_buffer_index =
					buffer_map[elem->vertex_buffer_index];
	}

	hash_key = of_draw_hash(&draw);
	vertex = cso_hash_find_data_from_template(ctx->draw_hash, hash_key,
							&draw, sizeof(draw));

	if (!vertex) {
		vertex = of_create_vertex_info(ctx, &draw, bypass_cache);
		cso_hash_insert(ctx->draw_hash, hash_key, vertex);
	} else if (dirty || index_dirty || LIST_IS_EMPTY(&vertex->buffers)) {
		while (dirty) {
			unsigned buffer = ffs(dirty) - 1;
			struct pipe_vertex_buffer *vb = &vertexbuf->vb[buffer];

			of_kill_draw_caches(ctx, vb->buffer);
			dirty &= ~(1 << buffer);
			of_resource(vb->buffer)->dirty = false;

			vertex->first_draw = true;
		}

		if (index_dirty) {
			of_kill_draw_caches(ctx, indexbuf->buffer);
			of_resource(indexbuf->buffer)->dirty = false;

			vertex->first_draw = true;
		}

		of_build_vertex_data(ctx, vertex);
	}

	of_emit_state(ctx, ctx->dirty);
	of_emit_draw(ctx, vertex);
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

	/* any buffers that haven't been cleared, we need to restore: */
	ctx->restore |= buffers & (OF_BUFFER_ALL & ~ctx->cleared);
	/* and any buffers used, need to be resolved: */
	ctx->resolve |= buffers;

	of_draw(ctx, info);
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
}

static void
of_clear_render_target(struct pipe_context *pctx, struct pipe_surface *ps,
		const union pipe_color_union *color,
		unsigned x, unsigned y, unsigned w, unsigned h)
{
	DBG("TODO: x=%u, y=%u, w=%u, h=%u", x, y, w, h);

	util_clear_render_target(pctx, ps, color, x, y, w, h);
}

static void
of_clear_depth_stencil(struct pipe_context *pctx, struct pipe_surface *ps,
		unsigned buffers, double depth, unsigned stencil,
		unsigned x, unsigned y, unsigned w, unsigned h)
{
	DBG("TODO: buffers=%u, depth=%f, stencil=%u, x=%u, y=%u, w=%u, h=%u",
			buffers, depth, stencil, x, y, w, h);

	util_clear_depth_stencil(pctx, ps, buffers, depth, stencil, x, y, w, h);
}

void
of_draw_init(struct pipe_context *pctx)
{
	struct of_context *ctx = of_context(pctx);

	pctx->draw_vbo = of_draw_vbo;
	pctx->clear = of_clear;
	pctx->clear_render_target = of_clear_render_target;
	pctx->clear_depth_stencil = of_clear_depth_stencil;

	ctx->draw_hash = cso_hash_create();
}
