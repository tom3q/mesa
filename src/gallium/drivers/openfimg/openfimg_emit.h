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

#ifndef OF_EMIT_H
#define OF_EMIT_H

#include "pipe/p_context.h"

#include "openfimg_context.h"

struct fd_ringbuffer;

struct of_vertex_buf {
	unsigned offset, size;
	struct pipe_resource *prsc;
};

enum g3d_shader_type {
	G3D_SHADER_VERTEX,
	G3D_SHADER_PIXEL,

	G3D_NUM_SHADERS
};

enum g3d_shader_data_type {
	G3D_SHADER_DATA_FLOAT,
	G3D_SHADER_DATA_INT,
	G3D_SHADER_DATA_BOOL,

	G3D_NUM_SHADER_DATA_TYPES
};

static inline uint32_t RSP_UNIT_NATTRIB(uint8_t unit, uint8_t nattrib)
{
	return (unit << 8) | nattrib;
}

static inline uint32_t RSP_DCOUNT(uint16_t type1, uint16_t type2)
{
	return (type2 << 16) | type1;
}

static inline uint32_t RSD_UNIT_TYPE_OFFS(uint8_t unit, uint8_t type,
					  uint16_t offs)
{
	return (unit << 24) | (type << 16) | offs;
}

void of_emit_state(struct of_context *ctx, uint32_t dirty);
void of_emit_setup(struct of_context *ctx);
void of_emit_setup_blit(struct of_context *ctx);
void of_emit_setup_solid(struct of_context *ctx);

#endif /* OF_EMIT_H */
