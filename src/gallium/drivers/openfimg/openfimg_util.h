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

#ifndef OPENFIMG_UTIL_H_
#define OPENFIMG_UTIL_H_

#include <freedreno_drmif.h>
#include <freedreno_ringbuffer.h>

#include "pipe/p_format.h"
#include "pipe/p_state.h"
#include "util/u_debug.h"
#include "util/u_math.h"

#include "fimg_3dse.xml.h"

#define OF_DBG_MSGS		0x1
#define OF_DBG_DISASM		0x2
#define OF_DBG_DCLEAR		0x4
#define OF_DBG_DGMEM		0x8
#define OF_DBG_VMSGS		0x10
#define OF_DBG_SHADER_OVERRIDE	0x20
extern int of_mesa_debug;

#define FORCE_DEBUG

#ifdef FORCE_DEBUG
#include <stdio.h>
#undef debug_printf
#define debug_printf _debug_printf
#endif

#define DBG(fmt, ...) \
		do { if (of_mesa_debug & OF_DBG_MSGS) \
			debug_printf("%s:%d: "fmt "\n", \
				__FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)

#define VDBG(fmt, ...) \
		do { if (of_mesa_debug & OF_DBG_VMSGS) \
			debug_printf("%s:%d: "fmt "\n", \
				__FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define ROUND_UP(val, to)	(((val) + (to) - 1) & ~((to) - 1))

#define min(a, b)	((a) < (b) ? (a) : (b))
#define max(a, b)	((a) > (b) ? (a) : (b))

/* for conditionally setting boolean flag(s): */
#define COND(bool, val) ((bool) ? (val) : 0)

#define LOG_DWORDS 0

#define MAX_MIP_LEVELS 12

enum of_request_type {
	G3D_REQUEST_REGISTER_WRITE = 0,
	G3D_REQUEST_SHADER_PROGRAM = 1,
	G3D_REQUEST_SHADER_DATA = 2,
	G3D_REQUEST_TEXTURE = 3,
#define G3D_TEXTURE_DIRTY	(1 << 0)
#define G3D_TEXTURE_DETACH	(1 << 1)
	G3D_REQUEST_COLORBUFFER = 4,
#define G3D_CBUFFER_DIRTY	(1 << 0)
#define G3D_CBUFFER_DETACH	(1 << 1)
	G3D_REQUEST_DEPTHBUFFER = 5,
#define G3D_DBUFFER_DIRTY	(1 << 0)
#define G3D_DBUFFER_DETACH	(1 << 1)
	G3D_REQUEST_DRAW = 6,
#define	G3D_DRAW_INDEXED	(1 << 31)
	G3D_REQUEST_VERTEX_BUFFER = 7,

	G3D_REQUEST_VTX_TEXTURE = -1,
};



enum fgtu_tex_format of_pipe2texture(enum pipe_format format);
enum fgpf_color_mode of_pipe2color(enum pipe_format format);
int of_depth_supported(enum pipe_format format);
uint32_t of_tex_swiz(enum pipe_format format, unsigned swizzle_r,
		unsigned swizzle_g, unsigned swizzle_b, unsigned swizzle_a);
enum pc_di_index_size of_pipe2index(enum pipe_format format);
enum fgpf_blend_factor of_blend_factor(unsigned factor);
enum fgpf_blend_op of_blend_func(unsigned func);
enum fgpf_stencil_action of_stencil_op(unsigned op);
enum fgpf_logical_op of_logic_op(unsigned op);
enum fgra_bfcull_face of_cull_face(unsigned face);
enum fgpf_test_mode of_test_mode(unsigned mode);

uint32_t of_hash_add(uint32_t hash, const void *data, size_t size);
uint32_t of_hash_finish(uint32_t hash);

static inline uint32_t of_hash_oneshot(const void *data, size_t size)
{
	return of_hash_finish(of_hash_add(0, data, size));
}

/* convert x,y to dword */
static inline uint32_t xy2d(uint16_t x, uint16_t y)
{
	return ((y & 0x3fff) << 16) | (x & 0x3fff);
}

static inline void
OUT_RING(struct fd_ringbuffer *ring, uint32_t data)
{
	if (LOG_DWORDS) {
		DBG("ring[%p]: OUT_RING   %04x:  %08x", ring,
				(uint32_t)(ring->cur - ring->last_start), data);
	}
	*(ring->cur++) = data;
}

static inline uint32_t *
OUT_PKT(struct fd_ringbuffer *ring, uint8_t opcode)
{
	uint32_t *pkt = ring->cur;
	uint32_t val = opcode << 24;
#ifdef DEBUG
	val |= 0xfa11ed;
#endif
	OUT_RING(ring, val);

	return pkt;
}

static inline void
END_PKT(struct fd_ringbuffer *ring, uint32_t *pkt)
{
	assert(pkt >= ring->last_start && pkt < ring->cur);
#ifdef DEBUG
	assert((*pkt & 0xffffff) == 0xfa11ed);
	*pkt &= ~0xffffff;
#endif
	*pkt |= ((ring->cur - pkt) - 1);
}

static inline enum pipe_format
pipe_surface_format(struct pipe_surface *psurf)
{
	if (!psurf)
		return PIPE_FORMAT_NONE;
	return psurf->format;
}

#endif /* OPENFIMG_UTIL_H_ */
