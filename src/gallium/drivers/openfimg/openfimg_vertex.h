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
#include <indices/u_indices.h>

#include "openfimg_context.h"

#define VERTEX_BUFFER_SIZE	4096

#define FGHI_ATTRIB(i)		(FGHI_ATTRIB0 + (i))
#define FGHI_ATTRIB_VBCTRL(i)	(FGHI_ATTRIB_VBCTRL0 + (i))
#define FGHI_ATTRIB_VBBASE(i)	(FGHI_ATTRIB_VBBASE0 + (i))

#define VERTEX_BUFFER_CONST	(MAX_WORDS_PER_VERTEX)
#define VERTEX_BUFFER_WORDS	(VERTEX_BUFFER_SIZE / 4 - VERTEX_BUFFER_CONST)

#define MAX_ATTRIBS		(OF_MAX_ATTRIBS)
#define MAX_WORDS_PER_ATTRIB	(4)
#define MAX_WORDS_PER_VERTEX	(MAX_ATTRIBS*MAX_WORDS_PER_ATTRIB)

#define CONST_ADDR(attrib)	(4*MAX_WORDS_PER_ATTRIB*(attrib))
#define DATA_OFFSET		(CONST_ADDR(MAX_ATTRIBS))

struct of_vertex_transfer {
	unsigned vertex_buffer_index;
	uint32_t src_offset;
	uint16_t offset;
	uint8_t width;
};

struct of_vertex_element {
	uint32_t attrib;
	uint32_t vbctrl;
	uint32_t vbbase;
};

struct of_vertex_stateobj {
	struct pipe_vertex_element pipe[OF_MAX_ATTRIBS];
	struct of_vertex_element elements[OF_MAX_ATTRIBS];
	struct of_vertex_transfer transfers[OF_MAX_ATTRIBS];
	unsigned num_elements;
	unsigned num_transfers;
	unsigned batch_size;
	bool ugly:1;
};

struct of_draw_info_base {
	struct pipe_draw_info info;
	const struct of_vertex_stateobj *vtx;
	unsigned num_vb;
};

struct of_draw_info {
	struct of_draw_info_base base;
	struct pipe_vertex_buffer vb[PIPE_MAX_ATTRIBS];
	struct pipe_index_buffer ib;
};

struct of_vertex_info {
	struct of_draw_info key;

	struct list_head buffers;
	unsigned draw_mode;
	bool first_draw:1;
	bool bypass_cache:1;
	bool indexed:1;

	unsigned num_draws;

	unsigned mode;
	unsigned index_size;
	unsigned count;
	u_translate_func trans_func;
	u_generate_func gen_func;
	struct pipe_index_buffer ib;
};

struct of_vertex_data {
	struct of_context *ctx;
	struct of_vertex_info *info;

	const void *const_data;
	unsigned const_size;

	const void *transfers[OF_MAX_ATTRIBS];
};

struct of_vertex_buffer {
	struct pipe_resource *buffer;
	uint32_t handle;
	unsigned nr_vertices;
	unsigned bytes_used;
	struct list_head list;
};

struct of_vertex_buffer *of_get_batch_buffer(struct of_context *ctx);
void of_put_batch_buffer(struct of_context *ctx, struct of_vertex_buffer *buf);

void of_prepare_draw_idx8(struct of_vertex_data *vdata,
			  const uint8_t *indices);
void of_prepare_draw_idx16(struct of_vertex_data *vdata,
			   const uint16_t *indices);
void of_prepare_draw_idx32(struct of_vertex_data *vdata,
			   const uint32_t *indices);
void of_prepare_draw_seq(struct of_vertex_data *vdata);

#endif
