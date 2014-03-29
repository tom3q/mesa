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

#define CBUF_ADDR_32(buf, offs)	\
			((const uint32_t *)((const uint8_t *)(buf) + (offs)))
#define CBUF_ADDR_16(buf, offs)	\
			((const uint16_t *)((const uint8_t *)(buf) + (offs)))
#define CBUF_ADDR_8(buf, offs)	\
			((const uint8_t *)(buf) + (offs))

#define BUF_ADDR_32(buf, offs)	\
			((uint32_t *)((uint8_t *)(buf) + (offs)))
#define BUF_ADDR_16(buf, offs)	\
			((uint16_t *)((uint8_t *)(buf) + (offs)))
#define BUF_ADDR_8(buf, offs)	\
			((uint8_t *)(buf) + (offs))

struct of_transfer_data {
	uint8_t *buf;
	const void *pointer;
	unsigned stride;
};

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
	/** How many vertices to skip. */
	unsigned shift;
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
		.multiple_of_two = 1,
		.repeat_last = 1,
	},
	[PIPE_PRIM_TRIANGLE_FAN] = {
		.min = 3,
		.overlap = 2,
		.extra = 2,
		.shift = 1,
		.repeat_first = 1,
	},
	[PIPE_PRIM_TRIANGLES] = {
		.min = 3,
		.multiple_of_three = 1,
	},
};

struct of_vertex_buffer *of_get_batch_buffer(struct of_context *ctx)
{
	struct of_vertex_buffer *buffer = CALLOC_STRUCT(of_vertex_buffer);

	if (buffer == NULL)
		return NULL;

	buffer->buffer = pipe_buffer_create(ctx->base.screen,
						PIPE_BIND_CUSTOM,
						PIPE_USAGE_IMMUTABLE,
						VERTEX_BUFFER_SIZE);
	buffer->handle = fd_bo_handle(of_resource(buffer->buffer)->bo);

	return buffer;
}

void of_put_batch_buffer(struct of_context *ctx, struct of_vertex_buffer *buf)
{
	LIST_ADDTAIL(&buf->list, &ctx->pending_batches);
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
