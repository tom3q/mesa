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

#ifndef INDEX_TYPE
#error INDEX_TYPE must be defined before including this file.
#endif

#ifndef SUFFIX
#error SUFFIX must be defined before including this file.
#endif

#undef PACK_ATTRIBUTE
#undef COPY_VERTICES
#undef PREPARE_DRAW

#define MAKE_FN_NAME(b,s)	b ## _ ## s
#define FN_NAME(base, suffix)	MAKE_FN_NAME(base, suffix)

#define PACK_ATTRIBUTE		FN_NAME(of_pack_attribute, SUFFIX)
#define COPY_VERTICES		FN_NAME(of_copy_vertices, SUFFIX)
#define PREPARE_DRAW		FN_NAME(__of_prepare_draw, SUFFIX)

/**
 * Packs attribute data into words (uint16_t indexed variant).
 * @param buf Destination buffer.
 * @param xfer Vertex transfer descriptor.
 * @param idx Array of vertex indices.
 * @param cnt Vertex count.
 * @return Size (in bytes) of packed data.
 */
static unsigned
PACK_ATTRIBUTE(uint8_t *dst, const void *src, unsigned stride,
	       uint8_t src_width, INDEX_TYPE idx, unsigned cnt)
{
	const uint8_t *data;
	unsigned size;
	unsigned width;

	/* Vertices must be word aligned */
#ifdef SEQUENTIAL
	data = CBUF_ADDR_8(src, idx * stride);
#endif
	width = ROUND_UP(src_width, 4);
	size = width * cnt;

	while (cnt--) {
		unsigned len = src_width;
#ifndef SEQUENTIAL
		data = CBUF_ADDR_8(src, *(idx++) * stride);
#endif
		small_memcpy(dst, data, len);
		dst += width;
#ifdef SEQUENTIAL
		data += stride;
#endif
	}

	return size;
}

/**
 * Prepares input vertex data for hardware processing.
 * @param ctx Hardware context.
 * @param arrays Array of attribute array descriptors.
 * @param indices Array of vertex indices.
 * @param pos Pointer to index of first vertex index.
 * @param count Pointer to count of unprocessed vertices.
 * @param prim_data Primitive type information.
 * @return Amount of vertices available to send to hardware.
 */
static void
COPY_VERTICES(struct of_vertex_data *vdata, INDEX_TYPE indices,
	      unsigned pos, unsigned count)
{
	struct of_context *ctx = vdata->ctx;
	struct of_vertex_info *vertex = vdata->info;
	const struct of_draw_info *draw = &vertex->key;
	const struct of_vertex_stateobj *vtx = draw->base.vtx;
	const struct of_primitive_data *prim_data;
	struct pipe_transfer *dst_transfer = NULL;
	struct of_vertex_buffer *buffer;
	struct pipe_resource *resource;
	const struct of_vertex_transfer *t;
	uint32_t handle;
	unsigned i;
	void *dst;

	prim_data = &primitive_data[vertex->mode];

	resource = pipe_buffer_create(ctx->base.screen, PIPE_BIND_CUSTOM,
					PIPE_USAGE_IMMUTABLE,
					VERTEX_BUFFER_SIZE);
	assert(resource);
	handle = fd_bo_handle(of_resource(resource)->bo);

	dst = pipe_buffer_map(&ctx->base, resource, PIPE_TRANSFER_WRITE,
				&dst_transfer);
	assert(dst);

	for (i = 0, t = vtx->transfers; i < vtx->num_transfers; ++t, ++i) {
		unsigned pipe_idx = t->vertex_buffer_index;
		unsigned buf_idx = vtx->vb_map[pipe_idx];
		unsigned stride = draw->vb_strides[buf_idx];
		const void *pointer = vdata->transfers[i];
		uint8_t *buf = BUF_ADDR_8(dst, t->offset);
		uint8_t width = t->width;

		buffer = CALLOC_STRUCT(of_vertex_buffer);
		assert(buffer);

		pipe_resource_reference(&buffer->buffer, resource);
		buffer->handle = handle;
		buffer->offset = t->offset;
		buffer->ctrl_dst_offset = t->offset;
		buffer->cmd = G3D_REQUEST_VERTEX_BUFFER;

#ifdef SEQUENTIAL
		if (ROUND_UP(width, 4) == stride) {
			if (prim_data->repeat_first) {
				memcpy(buf, CBUF_ADDR_8(pointer,
					indices * stride), width);
				buf += stride;
				memcpy(buf, CBUF_ADDR_8(pointer,
					indices * stride), width);
				buf += stride;
				memcpy(buf, CBUF_ADDR_8(pointer,
					indices * stride), width);
				buf += stride;
			}

			memcpy(buf, CBUF_ADDR_8(pointer,
				(indices + pos) * stride),
				count * stride);
			buf += count * stride;

			if (prim_data->repeat_last) {
				memcpy(buf, CBUF_ADDR_8(pointer,
					(indices + pos + count - 1) * stride),
					width);
				buf += stride;
			}

			buffer->length = ROUND_UP(buf -
						BUF_ADDR_8(dst, t->offset), 32);
			of_draw_add_buffer(buffer, vertex);
			continue;
		}
#endif
		if (prim_data->repeat_first) {
			buf += PACK_ATTRIBUTE(buf, pointer, stride, width,
						indices, 1);
			buf += PACK_ATTRIBUTE(buf, pointer, stride, width,
						indices, 1);
			buf += PACK_ATTRIBUTE(buf, pointer, stride, width,
						indices, 1);
		}

		buf += PACK_ATTRIBUTE(buf, pointer, stride, width,
					indices + pos, count);

		if (prim_data->repeat_last)
			buf += PACK_ATTRIBUTE(buf, pointer, stride, width,
						indices + pos + count - 1, 1);

		buffer->length = ROUND_UP(buf - BUF_ADDR_8(dst, t->offset), 32);
		of_draw_add_buffer(buffer, vertex);
	}

	buffer = CALLOC_STRUCT(of_vertex_buffer);
	assert(buffer);

	buffer->length = count + prim_data->extra;
	buffer->cmd = G3D_REQUEST_DRAW;
	of_draw_add_buffer(buffer, vertex);

	if (dst_transfer)
		pipe_buffer_unmap(&ctx->base, dst_transfer);

	pipe_resource_reference(&resource, NULL);
}

/**
 * Draws a sequence of vertices described by array descriptors.
 * @param vdata Vertex data processing request descriptor.
 * @param indices Pointer to first index OR offset of first vertex.
 */
static int
PREPARE_DRAW(struct of_vertex_data *vdata, INDEX_TYPE indices)
{
	struct of_vertex_info *vertex = vdata->info;
	const struct of_draw_info *draw = &vertex->key;
	const struct of_vertex_stateobj *vtx = draw->base.vtx;
	const struct of_primitive_data *prim;
	unsigned remaining;
	unsigned offset;

	prim = &primitive_data[vertex->mode];
	LIST_INITHEAD(&vertex->buffers);
	remaining = vertex->count;
	offset = 0;

	while (1) {
		unsigned effective = remaining + prim->extra;
		unsigned count = min(vtx->batch_size, effective);
		unsigned vtx_count;

		if (prim->multiple_of_two)
			count -= count % 2;
		if (prim->multiple_of_three)
			count -= count % 3;
		if (count < effective && prim->not_multiple_of_two)
			count -= 1 - count % 2;

		if (count < prim->min)
			break;

		vtx_count = count - prim->extra;

		COPY_VERTICES(vdata, indices, offset, vtx_count);

		if (vtx_count == remaining)
			break;

		remaining -= vtx_count - prim->overlap;
		offset += vtx_count - prim->overlap;
	}

	return 0;
}
