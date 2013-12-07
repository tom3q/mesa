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

#define FGHI_ATTRIB(i)		(FGHI_ATTRIB0 + (i))
#define FGHI_ATTRIB_VBCTRL(i)	(FGHI_ATTRIB_VBCTRL0 + (i))
#define FGHI_ATTRIB_VBBASE(i)	(FGHI_ATTRIB_VBBASE0 + (i))

#define VERTEX_BUFFER_CONST	(MAX_WORDS_PER_VERTEX)
#define VERTEX_BUFFER_WORDS	(VERTEX_BUFFER_SIZE / 4 - VERTEX_BUFFER_CONST)

#define MAX_ATTRIBS		(FIMG_ATTRIB_NUM)
#define MAX_WORDS_PER_ATTRIB	(4)
#define MAX_WORDS_PER_VERTEX	(MAX_ATTRIBS*MAX_WORDS_PER_ATTRIB)

#define CONST_ADDR(attrib)	(4*MAX_WORDS_PER_ATTRIB*(attrib))
#define DATA_OFFSET		(CONST_ADDR(MAX_ATTRIBS))

#define ROUND_UP(val, to)	(((val) + (to) - 1) & ~((to) - 1))

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

/** Primitive types supported by FIMG-3DSE. */
enum of_primitive_type {
	FGPE_POINT_SPRITE = 0,	/**< Point sprites */
	FGPE_POINTS,		/**< Separate points */
	FGPE_LINE_STRIP,	/**< Line strips */
	FGPE_LINE_LOOP,		/**< Line loops */
	FGPE_LINES,		/**< Separate lines */
	FGPE_TRIANGLE_STRIP,	/**< Triangle strips */
	FGPE_TRIANGLE_FAN,	/**< Triangle fans */
	FGPE_TRIANGLES,		/**< Separate triangles */
	FGPE_PRIMITIVE_MAX,
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
	unsigned multipleOfTwo:1;
	/** Set to 1 if batch size must be a multiple of three. */
	unsigned multipleOfThree:1;
	/** Set to 1 if first vertex must be repeated three times. */
	unsigned repeatFirst:1;
	/** Set to 1 if last vertex must be repeated. */
	unsigned repeatLast:1;
};

struct of_batch_buffer {
	uint8_t *base;
	unsigned nr_vertices;
	struct list_head list;
};

/**
 * Structure describing requirements of primitive mode regarding
 * geometry format.
 */
const struct of_primitive_data primitive_data[FGPE_PRIMITIVE_MAX] = {
	[FGPE_POINT_SPRITE] = {
		.min = 1,
	},
	[FGPE_POINTS] = {
		.min = 1,
	},
	[FGPE_LINE_STRIP] = {
		.min = 2,
		.overlap = 1,
	},
	[FGPE_LINE_LOOP] = {
		/*
		 * Line loops don't go well with buffered transfers,
		 * so let's just force higher level code to emulate them
		 * using line strips.
		 */
		.min = 0,
	},
	[FGPE_LINES] = {
		.min = 2,
		.multipleOfTwo = 1,
	},
	[FGPE_TRIANGLE_STRIP] = {
		.min = 3,
		.overlap = 2,
		.extra = 1,
		.multipleOfTwo = 1,
		.repeatLast = 1,
	},
	[FGPE_TRIANGLE_FAN] = {
		.min = 3,
		.overlap = 2,
		.extra = 2,
		.shift = 1,
		.repeatFirst = 1,
	},
	[FGPE_TRIANGLES] = {
		.min = 3,
		.multipleOfThree = 1,
	},
};

static struct of_batch_buffer *of_get_batch_buffer(struct of_context *ctx)
{
#warning TODO
	return NULL;
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
void of_prepare_draw_idx8(struct of_context *ctx, struct of_draw_info *draw,
			  unsigned count, const uint8_t *indices)
{
	PREPARE_DRAW(ctx, draw, count, indices);
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
void of_prepare_draw_idx16(struct of_context *ctx, struct of_draw_info *draw,
			   unsigned count, const uint16_t *indices)
{
	PREPARE_DRAW(ctx, draw, count, indices);
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
void of_prepare_draw_idx32(struct of_context *ctx, struct of_draw_info *draw,
			   unsigned count, const uint32_t *indices)
{
	PREPARE_DRAW(ctx, draw, count, indices);
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
void of_prepare_draw_seq(struct of_context *ctx, struct of_draw_info *draw,
			 unsigned int count)
{
	PREPARE_DRAW(ctx, draw, count, 0);
}

#undef INDEX_TYPE
#undef SEQUENTIAL
#undef SUFFIX
