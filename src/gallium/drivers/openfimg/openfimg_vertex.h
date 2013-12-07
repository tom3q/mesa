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

#ifndef OPENFIMG_VERTEX_H_
#define OPENFIMG_VERTEX_H_

#include <stdint.h>

#include <util/u_double_list.h>

#define VERTEX_BUFFER_SIZE	4096
#define FIMG_ATTRIB_NUM		9

struct of_vertex_transfer {
	/** Pointer to vertex attribute data. */
	const void *pointer;
	/** Stride of single vertex. */
	uint8_t stride;
	/** Width of single vertex. */
	uint8_t width;
	/** Offset in batch buffer */
	uint16_t offset;
};

struct of_draw_info {
	unsigned batch_size;
	unsigned mode;
	unsigned num_transfers;
	struct of_vertex_transfer transfers[FIMG_ATTRIB_NUM];
	struct list_head buffers;
};

void of_prepare_draw_idx8(struct of_context *ctx, struct of_draw_info *draw,
			  unsigned count, const uint8_t *indices);
void of_prepare_draw_idx16(struct of_context *ctx, struct of_draw_info *draw,
			   unsigned count, const uint16_t *indices);
void of_prepare_draw_idx32(struct of_context *ctx, struct of_draw_info *draw,
			   unsigned count, const uint32_t *indices);
void of_prepare_draw_seq(struct of_context *ctx, struct of_draw_info *draw,
			 unsigned int count);

#endif
