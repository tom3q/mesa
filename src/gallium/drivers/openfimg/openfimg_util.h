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

//#include <openfimg_drmif.h>
//#include <openfimg_ringbuffer.h>

#include "pipe/p_format.h"
#include "util/u_debug.h"
#include "util/u_math.h"

#include "fimg_3dse.xml.h"

#define OF_DBG_MSGS   0x1
#define OF_DBG_DISASM 0x2
#define OF_DBG_DCLEAR 0x4
#define OF_DBG_DGMEM  0x8
extern int of_mesa_debug;

#define DBG(fmt, ...) \
		do { if (of_mesa_debug & OF_DBG_MSGS) \
			debug_printf("%s:%d: "fmt "\n", \
				__FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* for conditionally setting boolean flag(s): */
#define COND(bool, val) ((bool) ? (val) : 0)

#define LOG_DWORDS 0

enum fgtu_tex_format of_pipe2texture(enum pipe_format format);
enum fgpf_color_mode of_pipe2color(enum pipe_format format);
int of_depth_supported(enum pipe_format format);
uint32_t of_tex_swiz(enum pipe_format format, unsigned swizzle_r,
		unsigned swizzle_g, unsigned swizzle_b, unsigned swizzle_a);
enum pc_di_index_size of_pipe2index(enum pipe_format format);
enum fgpf_blend_factor of_blend_factor(unsigned factor);
enum fgpf_blend_op of_blend_func(unsigned func);
enum adreno_pa_su_sc_draw of_polygon_mode(unsigned mode);
enum fgpf_stencil_action of_stencil_op(unsigned op);

/* convert x,y to dword */
static inline uint32_t xy2d(uint16_t x, uint16_t y)
{
	return ((y & 0x3fff) << 16) | (x & 0x3fff);
}

#endif /* OPENFIMG_UTIL_H_ */
