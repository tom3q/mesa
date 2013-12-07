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
PACK_ATTRIBUTE(uint8_t *buf, const struct of_vertex_transfer *xfer,
	       INDEX_TYPE idx, unsigned cnt)
{
	const uint8_t *data;
	unsigned size;
	unsigned width;

	/* Vertices must be word aligned */
#ifdef SEQUENTIAL
	data = CBUF_ADDR_8(xfer->pointer, idx * xfer->stride);
#endif
	width = ROUND_UP(xfer->width, 4);
	size = width * cnt;

	while (cnt--) {
		unsigned len = xfer->width;
#ifndef SEQUENTIAL
		data = CBUF_ADDR_8(xfer->pointer, *(idx++) * xfer->stride);
#endif
		small_memcpy(buf, data, len);
		buf += width;
#ifdef SEQUENTIAL
		data += xfer->stride;
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
static struct of_batch_buffer *
COPY_VERTICES(struct of_context *ctx, struct of_draw_info *draw,
	      INDEX_TYPE indices, unsigned *pos, unsigned *count)
{
	const struct of_primitive_data *prim_data;
	struct of_batch_buffer *buffer;
	struct of_vertex_transfer *t;
	unsigned batch_size;
	uint8_t *buf;
	unsigned i;

	prim_data = &primitive_data[draw->mode];
	batch_size = draw->batch_size - prim_data->extra;

	if (batch_size > *count)
		batch_size = *count;

	if (batch_size < prim_data->min)
		return 0;

	if (batch_size > prim_data->min) {
		if (prim_data->multipleOfTwo && (batch_size % 2))
			--batch_size;
		if (prim_data->multipleOfThree && (batch_size % 3))
			batch_size -= batch_size % 3;
	}

	buffer = of_get_batch_buffer(ctx);

	for (i = 0, t = draw->transfers; i < draw->num_transfers; ++t, ++i) {
		buf = BUF_ADDR_8(buffer->base, t->offset);
#ifdef SEQUENTIAL
		if (ROUND_UP(t->width, 4) == t->stride) {
			if (prim_data->repeatFirst) {
				memcpy(buf, t->pointer, t->width);
				buf += t->stride;
				memcpy(buf, t->pointer, t->width);
				buf += t->stride;
				memcpy(buf, t->pointer, t->width);
				buf += t->stride;
			}

			memcpy(buf, BUF_ADDR_8(t->pointer,
				(*pos + prim_data->shift) * t->stride),
				(batch_size - prim_data->shift) * t->stride);
			buf += (batch_size - prim_data->shift) * t->stride;

			if (prim_data->repeatLast) {
				memcpy(buf, BUF_ADDR_8(t->pointer,
					(*pos + batch_size - 1) * t->stride),
					t->width);
				buf += t->stride;
			}

			continue;
		}
#endif
		if (prim_data->repeatFirst) {
			buf += PACK_ATTRIBUTE(buf, t, indices, 1);
			buf += PACK_ATTRIBUTE(buf, t, indices, 1);
			buf += PACK_ATTRIBUTE(buf, t, indices, 1);
		}

		buf += PACK_ATTRIBUTE(buf, t, indices + *pos + prim_data->shift,
					batch_size - prim_data->shift);

		if (prim_data->repeatLast)
			buf += PACK_ATTRIBUTE(buf, t,
					indices + *pos + batch_size - 1, 1);
	}

	*pos += batch_size - prim_data->overlap;
	*count -= batch_size - prim_data->overlap;
	buffer->nr_vertices = batch_size + prim_data->extra;

	return buffer;
}

/**
 * Draws a sequence of vertices described by array descriptors.
 * @param ctx Hardware context.
 * @param mode Primitive type.
 * @param arrays Array of attribute array descriptors.
 * @param count Vertex count.
 * @param indices First index.
 */
static int
PREPARE_DRAW(struct of_context *ctx, struct of_draw_info *draw,
	     unsigned count, INDEX_TYPE indices)
{
	struct of_batch_buffer *buffer;
	unsigned int pos = 0;

	LIST_INITHEAD(&draw->buffers);

	do {
		buffer = COPY_VERTICES(ctx, draw, indices, &pos, &count);
		if (!buffer)
			break;
		LIST_ADDTAIL(&buffer->list, &draw->buffers);
	} while (1);

	return 0;
}
