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
#include "openfimg_state.h"

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
	uint32_t src_offset;
	uint16_t offset;
	uint8_t width;
	uint8_t vertex_buffer_index;
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
	struct of_cso cso;
	unsigned vb_mask;
	uint16_t batch_size;
	uint8_t vb_map[PIPE_MAX_ATTRIBS];
	uint8_t num_vb;
	uint8_t num_elements;
	uint8_t num_transfers;
	bool ugly:1;
};

struct of_draw_info_base {
	struct pipe_draw_info info;
	struct of_vertex_stateobj *vtx;
	unsigned vb_mask;
	uint8_t num_vb;
};

struct of_draw_info {
	struct of_draw_info_base base;
	struct pipe_vertex_buffer vb[OF_MAX_ATTRIBS];
	uint8_t vb_strides[OF_MAX_ATTRIBS];
	struct pipe_index_buffer ib;

	/* Unhashed */
	bool user_ib:1;
	bool user_vb:1;
};

struct of_vertex_info {
	struct of_draw_info key;

	struct list_head buffers;
	unsigned draw_mode;
	bool first_draw:1;
	bool bypass_cache:1;
	bool indexed:1;
	bool direct:1;

	unsigned num_draws;

	unsigned mode;
	unsigned count;
	u_translate_func trans_func;
	u_generate_func gen_func;
	struct pipe_index_buffer ib;

	uint32_t ib_version;
	uint32_t vb_version[OF_MAX_ATTRIBS];
	struct pipe_resource *rscs[OF_MAX_ATTRIBS + 1];

	struct list_head lru_list;
	unsigned last_use;
};

struct of_vertex_data {
	struct of_context *ctx;
	struct of_vertex_info *info;

	const void *transfers[OF_MAX_ATTRIBS];
};

struct of_vertex_buffer {
	struct list_head list;
	struct pipe_resource *buffer;
	unsigned cmd;
	unsigned length;
	unsigned handle;
	unsigned offset;
	unsigned ctrl_dst_offset;
	uint8_t vb_idx;
	bool direct:1;
};

//struct of_vertex_buffer *of_get_batch_buffer(struct of_context *ctx);
void of_put_batch_buffer(struct of_context *ctx, struct of_vertex_buffer *buf);

void of_prepare_draw_direct(struct of_vertex_data *vdata);
void of_prepare_draw_direct_wa(struct of_vertex_data *vdata);
bool of_prepare_draw_direct_indices(struct of_vertex_data *vdata,
				    const void *indices);

void of_prepare_draw_idx8(struct of_vertex_data *vdata,
			  const uint8_t *indices);
void of_prepare_draw_idx16(struct of_vertex_data *vdata,
			   const uint16_t *indices);
void of_prepare_draw_idx32(struct of_vertex_data *vdata,
			   const uint32_t *indices);
void of_prepare_draw_seq(struct of_vertex_data *vdata);

static INLINE void
of_draw_add_buffer(struct of_vertex_buffer *buffer,
		   struct of_vertex_info *vertex)
{
	assert(buffer->cmd != G3D_REQUEST_VERTEX_BUFFER
		|| (buffer->length > 0
		&& buffer->ctrl_dst_offset + buffer->length
		<= VERTEX_BUFFER_SIZE));

	LIST_ADDTAIL(&buffer->list, &vertex->buffers);
}

#endif
