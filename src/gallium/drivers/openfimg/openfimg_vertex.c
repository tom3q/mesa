/*
 * Copyright (C) 2013 Tomasz Figa <tomasz.figa@gmail.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>

#include "openfimg_draw.h"
#include "openfimg_emit.h"
#include "openfimg_context.h"
#include "openfimg_state.h"
#include "openfimg_resource.h"
#include "openfimg_util.h"
#include "openfimg_vertex.h"

#include <util/u_double_list.h>

/**
 * Structure describing requirements of primitive mode regarding
 * geometry format.
 */
struct of_primitive_data {
	/**
	 * Minimal count of vertices in batch.
	 * Set to 0 if given primitive type is not supported.
	 */
	unsigned min;
	/** How many vertices of previous batch overlap with next batch. */
	unsigned overlap;
	/** Vertices in batch reserved for extra data. */
	unsigned extra;
	/** Set to 1 if batch size must not be a multiple of two. */
	unsigned not_multiple_of_two:1;
	/** Set to 1 if batch size must be a multiple of two. */
	unsigned multiple_of_two:1;
	/** Set to 1 if batch size must be a multiple of three. */
	unsigned multiple_of_three:1;
	/** Set to 1 if first vertex must be repeated three times. */
	unsigned repeat_first:1;
	/** Set to 1 if last vertex must be repeated. */
	unsigned repeat_last:1;
};

/**
 * Structure describing requirements of primitive mode regarding
 * geometry format.
 */
const struct of_primitive_data primitive_data[PIPE_PRIM_MAX] = {
	[PIPE_PRIM_POINTS] = {
		.min = 1,
	},
	[PIPE_PRIM_LINE_STRIP] = {
		.min = 2,
		.overlap = 1,
	},
	[PIPE_PRIM_LINES] = {
		.min = 2,
		.multiple_of_two = 1,
	},
	[PIPE_PRIM_TRIANGLE_STRIP] = {
		.min = 3,
		.overlap = 2,
		.extra = 1,
		.repeat_last = 1,
		.not_multiple_of_two = 1,
	},
	[PIPE_PRIM_TRIANGLE_FAN] = {
		.min = 3,
		.overlap = 1,
		.extra = 3,
		.repeat_first = 1,
	},
	[PIPE_PRIM_TRIANGLES] = {
		.min = 3,
		.multiple_of_three = 1,
	},
};

void of_put_batch_buffer(struct of_context *ctx, struct of_vertex_buffer *buf)
{
	list_del(&buf->list);
	pipe_resource_reference(&buf->buffer, NULL);
	FREE(buf);
}

/**
 * Copies small number of bytes from source buffer to destination buffer.
 * @param dst Destination buffer.
 * @param src Source buffer.
 * @param len Number of buffers to copy.
 */
static void small_memcpy(uint8_t *dst, const uint8_t *src, uint32_t len)
{
	while (len >= 4) {
		memcpy(dst, src, 4);
		dst += 4;
		src += 4;
		len -= 4;
	}

	while (len--)
		*(dst++) = *(src++);
}

static void
emit_transfers(struct of_vertex_info *vertex, uint32_t offset, uint32_t count,
	       uint32_t dst_offset)
{
	const struct of_draw_info *draw = &vertex->key;
	const struct of_vertex_stateobj *vtx = draw->base.vtx;
	const struct of_vertex_transfer *transfer;
	struct of_vertex_buffer *buf;
	unsigned i;

	transfer = vtx->transfers;
	for (i = 0; i < vtx->num_transfers; ++i, ++transfer) {
		unsigned pipe_idx = transfer->vertex_buffer_index;
		unsigned buf_idx = vtx->vb_map[pipe_idx];
		const struct pipe_vertex_buffer *vb =
						&draw->vb[buf_idx];

		buf = CALLOC_STRUCT(of_vertex_buffer);
		assert(buf);

		buf->direct = true;
		buf->vb_idx = pipe_idx;
		buf->length = vb->stride * count;
		buf->offset = transfer->src_offset + vb->stride * offset;
		buf->ctrl_dst_offset = transfer->offset
				+ vb->stride * dst_offset;
		buf->cmd = G3D_REQUEST_VERTEX_BUFFER;

		of_draw_add_buffer(buf, vertex);
	}
}

/*
 * Fast path for aligned, sequential vertex data and primitive types
 * handled by hardware properly.
 *
 * VBOs are used directly to feed the GPU.
 */
void
of_prepare_draw_direct(struct of_vertex_data *vdata)
{
	struct of_vertex_info *vertex = vdata->info;
	const struct of_draw_info *draw = &vertex->key;
	const struct of_vertex_stateobj *vtx = draw->base.vtx;
	const struct of_primitive_data *prim;
	struct of_vertex_buffer *buf;
	unsigned remaining;
	unsigned offset;

	prim = &primitive_data[draw->base.info.mode];
	LIST_INITHEAD(&vertex->buffers);

	offset = draw->base.info.start;
	remaining = draw->base.info.count;
	while (remaining) {
		unsigned count = min(vtx->batch_size, remaining);

		if (prim->multiple_of_two)
			count -= count % 2;
		if (prim->multiple_of_three)
			count -= count % 3;

		if (count < prim->min)
			break;

		emit_transfers(vertex, offset, count, 0);

		buf = CALLOC_STRUCT(of_vertex_buffer);
		assert(buf);

		buf->length = count;
		buf->cmd = G3D_REQUEST_DRAW;

		of_draw_add_buffer(buf, vertex);

		remaining -= count - prim->overlap;
		offset += count - prim->overlap;
	}
}

#define IB_SIZE		4096

/*
 * Semi-fast path for aligned, sequential vertex data and primitive types
 * that require workarounds for HW bugs.
 *
 * VBOs are used directly to feed the GPU. Auxiliary indices are used to
 * handle hardware quirks.
 */
void
of_prepare_draw_direct_wa(struct of_vertex_data *vdata)
{
	struct of_vertex_info *vertex = vdata->info;
	const struct of_draw_info *draw = &vertex->key;
	const struct of_vertex_stateobj *vtx = draw->base.vtx;
	struct pipe_transfer *ib_transfer = NULL;
	const struct of_primitive_data *prim;
	struct pipe_resource *ib_buf = NULL;
	struct of_context *ctx = vdata->ctx;
	struct of_vertex_buffer *buf;
	unsigned ib_offset = IB_SIZE;
	unsigned dst_offset = 0;
	uint32_t ib_handle = 0;
	void *ib_ptr = NULL;
	unsigned batch_size;
	unsigned remaining;
	unsigned offset;

	prim = &primitive_data[draw->base.info.mode];
	LIST_INITHEAD(&vertex->buffers);

	offset = draw->base.info.start;
	remaining = draw->base.info.count;

	if (prim->repeat_first) {
		unsigned int min_stride = 0xffffffff;
		int i;

		for (i = 0; i < vtx->num_vb; ++i) {
			const struct pipe_vertex_buffer *vb = &draw->vb[i];

			min_stride = min(min_stride, vb->stride);
		}

		if (min_stride >= 16)
			dst_offset = 2;
		else
			dst_offset = (16 + min_stride - 1) / min_stride;

		emit_transfers(vertex, offset, 1, 0);
	}

	batch_size = min(vtx->batch_size - dst_offset, 124);

	while (1) {
		unsigned count = min(batch_size, remaining);
		unsigned idx_count = count + prim->extra;
		uint8_t *idx;
		int i;

		if (prim->multiple_of_two)
			idx_count -= idx_count % 2;
		if (prim->multiple_of_three)
			idx_count -= idx_count % 3;
		if (count < remaining && prim->not_multiple_of_two)
			idx_count -= 1 - idx_count % 2;

		if (idx_count < prim->min)
			break;

		count = idx_count - prim->extra;

		if (IB_SIZE - ib_offset < ROUND_UP(idx_count, 4)) {
			if (ib_transfer)
				pipe_buffer_unmap(&ctx->base, ib_transfer);

			ib_buf = pipe_buffer_create(ctx->base.screen,
							PIPE_BIND_CUSTOM,
							PIPE_USAGE_IMMUTABLE,
							IB_SIZE);
			ib_handle = fd_bo_handle(of_resource(ib_buf)->bo);
			ib_offset = 0;

			ib_ptr = pipe_buffer_map(&ctx->base, ib_buf,
							PIPE_TRANSFER_WRITE,
							&ib_transfer);
		}

		idx = BUF_ADDR_8(ib_ptr, ib_offset);
		if (prim->repeat_first)
			for (i = 0; i < 3; ++i)
				*(idx++) = 0;
		for (i = dst_offset; i < (count + dst_offset); ++i)
			*(idx++) = i;
		if (prim->repeat_last)
			*idx = count + dst_offset - 1;

		emit_transfers(vertex, offset, count, dst_offset);

		buf = CALLOC_STRUCT(of_vertex_buffer);
		assert(buf);

		pipe_resource_reference(&buf->buffer, ib_buf);
		buf->cmd = G3D_REQUEST_DRAW;
		buf->length = idx_count;
		buf->handle = ib_handle;
		buf->offset = ib_offset;
		buf->ctrl_dst_offset = G3D_DRAW_INDEXED;
		of_draw_add_buffer(buf, vertex);

		if (count == remaining)
			break;

		ib_offset += ROUND_UP(idx_count, 4);
		remaining -= count - prim->overlap;
		offset += count - prim->overlap;
	}

	if (ib_transfer)
		pipe_buffer_unmap(&ctx->base, ib_transfer);
}

#if 0
static const void *
get_next_index(const struct pipe_index_buffer *ib, const void *indices,
	       uint32_t *index)
{
	switch (ib->index_size) {
	case 1:
		*index = *(const uint8_t *)indices;
		return (const uint8_t *)indices + 1;
	case 2:
		*index = *(const uint16_t *)indices;
		return (const uint16_t *)indices + 1;
	case 4:
		*index = *(const uint32_t *)indices;
		return (const uint32_t *)indices + 1;
	default:
		assert(0);
		*index = 0;
		return indices;
	}
}

static const void *
skip_n_indices(const struct pipe_index_buffer *ib, const void *indices,
	       uint32_t n)
{
	switch (ib->index_size) {
	case 1:
		return (const uint8_t *)indices + n;
	case 2:
		return (const uint16_t *)indices + n;
	case 4:
		return (const uint32_t *)indices + n;
	default:
		assert(0);
		return indices;
	}
}

/**
 * Checks locality of indices in index buffer to determine whether it would
 * be efficient to use it directly.
 * @param draw Draw info structure.
 * @param ib Pipe index buffer descriptor.
 * @param indices Pointer to mapped index buffer.
 * @return true if IB should be used directly, false otherwise.
 */
static unsigned
of_check_index_ranges(const struct of_vertex_info *vertex,
		      const void *indices)
{
	const struct pipe_index_buffer *ib = &vertex->ib;
	const struct of_draw_info *draw = &vertex->key;
	unsigned min_vtx, max_vtx;
	unsigned num_parts;
	unsigned num_verts;
	unsigned i;

	/* Check if all vertices would fit into HW vertex buffer. */
	min_vtx = draw->base.info.min_index;
	max_vtx = draw->base.info.max_index;
	if (max_vtx - min_vtx <= draw->base.vtx->batch_size)
		return true;

	/* Try to partition indices into blocks that would fit. */
	max_vtx = 0;
	min_vtx = 0xffffffff;
	num_verts = 0;
	num_parts = 0;
	indices = skip_n_indices(ib, indices, draw->base.info.start);
	for (i = 0; i < vertex->count; ++i) {
		uint32_t index;

		indices = get_next_index(ib, indices, &index);

		min_vtx = min(min_vtx, index);
		max_vtx = max(max_vtx, index);

		if (max_vtx - min_vtx > draw->base.vtx->batch_size) {
			/* Exhausted HW vertex buffer space. */
			if (num_verts < 2)
				return 0;

			max_vtx = 0;
			min_vtx = 0xffffffff;
			num_verts = 0;
			++num_parts;

			continue;
		}

		/* Still fits. */
		++num_verts;
	}

	return num_parts;
}
#endif

/*
 * Semi-fast path for aligned vertex data, indices with reasonable locality and
 * primitive types without HW bugs.
 *
 * Both VBOs and IBOs are used directly to feed the GPU, but the draw needs to
 * be split into smaller batches such that all indexed vertices fit into
 * hardware vertex buffer.
 */
bool
of_prepare_draw_direct_indices(struct of_vertex_data *vdata,
			       const void *indices)
{
#if 0
	struct of_vertex_info *vertex = vdata->info;
	const struct of_draw_info *draw = &vertex->key;
	const struct of_vertex_stateobj *vtx = draw->base.vtx;
	const struct pipe_index_buffer *ib = &vertex->ib;
	const struct of_primitive_data *prim;
	unsigned min_vtx, max_vtx;
	unsigned first_idx;
	unsigned parts_limit;
	unsigned num_parts;
	unsigned num_verts;
	unsigned i;

	parts_limit = 4 * ((vertex->count + vtx->batch_size - 1)
							/ vtx->batch_size);

	num_parts = of_check_index_ranges(vertex, indices);
	if (!num_parts || num_parts > parts_limit)
		return false;

	prim = &primitive_data[draw->base.info.mode];
	LIST_INITHEAD(&vertex->buffers);

	/* Partition indices into blocks that will fit into HW buffer. */
	max_vtx = 0;
	min_vtx = 0xffffffff;
	num_verts = 0;
	first_idx = draw->base.info.start;
	indices = skip_n_indices(ib, indices, first_idx);
	for (i = 0; i < vertex->count; ++i) {
		unsigned new_min_vtx, new_max_vtx;
		struct of_vertex_buffer *buf;
		uint32_t index;

		indices = get_next_index(ib, indices, &index);

		new_min_vtx = min(min_vtx, index);
		new_max_vtx = max(max_vtx, index);

		if (new_max_vtx - new_min_vtx > draw->base.vtx->batch_size) {
			unsigned slot = 0;

			emit_transfers(vertex, min_vtx,
					max_vtx - min_vtx + 1, 0);

			buf = CALLOC_STRUCT(of_vertex_buffer);
			assert(buf);

			pipe_resource_reference(&buf->buffer, ib->buffer);
			buf->handle = fd_bo_handle(of_resource(ib->buffer)->bo);
			if (i == 0) {
				buf->nr_vertices_dst_offset = vertex->count;
				if (prim->repeat_first)
					buf->nr_vertices_dst_offset += 3;
				if (prim->repeat_last)
					++buf->nr_vertices_dst_offset;
			}
			buf->idx_offset = (uint32_t)-min_vtx;
			buf->idx_size = ib->index_size;

			if (i == 0 && prim->repeat_first) {
				buf->src_offset[slot] =
						ib->index_size * first_idx;
				buf->bytes_used[slot++] = ib->index_size;
				buf->src_offset[slot] =
						ib->index_size * first_idx;
				buf->bytes_used[slot++] = ib->index_size;
				buf->src_offset[slot] =
						ib->index_size * first_idx;
				buf->bytes_used[slot++] = ib->index_size;
			}

			buf->src_offset[slot] = ib->index_size * first_idx;
			buf->bytes_used[slot++] = ib->index_size * num_verts;

			if (i == vertex->count - 1 && prim->repeat_last) {
				buf->src_offset[slot] =
						ib->index_size * first_idx;
				buf->bytes_used[slot++] = ib->index_size;
			}

			buf->cmd = G3D_REQUEST_INDEX_BUFFER;
			of_draw_add_buffer(buf, vertex);

			max_vtx = 0;
			min_vtx = 0xffffffff;
			num_verts = 0;
			first_idx += num_verts;
			--num_parts;

			continue;
		}

		/* Still fits. */
		min_vtx = new_min_vtx;
		max_vtx = new_max_vtx;
		++num_verts;
	}

	return true;
#else
	/* FIXME */
	return false;
#endif
}

/*
 *
 */

/*
 * 8-bit indices
 */

#define SUFFIX		idx8
#define INDEX_TYPE	const uint8_t*
#undef SEQUENTIAL

#include "openfimg_vertex_template.h"

/**
 * Draws a sequence of vertices described by array descriptors and a sequence
 * of uint8_t indices.
 * @param ctx Hardware context.
 * @param mode Primitive type.
 * @param arrays Array of attribute array descriptors.
 * @param count Vertex count.
 * @param indices Array of vertex indices.
 */
void of_prepare_draw_idx8(struct of_vertex_data *vtx, const uint8_t *indices)
{
	PREPARE_DRAW(vtx, indices);
}

#undef INDEX_TYPE
#undef SEQUENTIAL
#undef SUFFIX

/*
 * 16-bit indices
 */

#define SUFFIX		idx16
#define INDEX_TYPE	const uint16_t*
#undef SEQUENTIAL

#include "openfimg_vertex_template.h"

/**
 * Draws a sequence of vertices described by array descriptors and a sequence
 * of uint16_t indices.
 * @param ctx Hardware context.
 * @param mode Primitive type.
 * @param arrays Array of attribute array descriptors.
 * @param count Vertex count.
 * @param indices Array of vertex indices.
 */
void of_prepare_draw_idx16(struct of_vertex_data *vtx, const uint16_t *indices)
{
	PREPARE_DRAW(vtx, indices);
}

#undef INDEX_TYPE
#undef SEQUENTIAL
#undef SUFFIX

/*
 * 32-bit indices
 */

#define SUFFIX		idx32
#define INDEX_TYPE	const uint32_t*
#undef SEQUENTIAL

#include "openfimg_vertex_template.h"

/**
 * Draws a sequence of vertices described by array descriptors and a sequence
 * of uint32_t indices.
 * @param ctx Hardware context.
 * @param mode Primitive type.
 * @param arrays Array of attribute array descriptors.
 * @param count Vertex count.
 * @param indices Array of vertex indices.
 */
void of_prepare_draw_idx32(struct of_vertex_data *vtx, const uint32_t *indices)
{
	PREPARE_DRAW(vtx, indices);
}

#undef INDEX_TYPE
#undef SEQUENTIAL
#undef SUFFIX

/*
 * Sequential
 */

#define SUFFIX		seq
#define INDEX_TYPE	uint32_t
#define SEQUENTIAL

#include "openfimg_vertex_template.h"

/**
 * Draws a sequence of vertices described by array descriptors.
 * @param ctx Hardware context.
 * @param mode Primitive type.
 * @param arrays Array of attribute array descriptors.
 * @param count Vertex count.
 */
void of_prepare_draw_seq(struct of_vertex_data *vtx)
{
	PREPARE_DRAW(vtx, vtx->info->key.base.info.start);
}

#undef INDEX_TYPE
#undef SEQUENTIAL
#undef SUFFIX
