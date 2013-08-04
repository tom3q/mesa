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

#include "pipe/p_defines.h"
#include "util/u_format.h"

#include "openfimg_util.h"

enum fgtu_tex_format
of_pipe2texture(enum pipe_format format)
{
	switch (format) {
	/* 8-bit buffers. */
	case PIPE_FORMAT_A8_UNORM:
	case PIPE_FORMAT_A8_SNORM:
	case PIPE_FORMAT_A8_UINT:
	case PIPE_FORMAT_A8_SINT:
	case PIPE_FORMAT_I8_UNORM:
	case PIPE_FORMAT_I8_SNORM:
	case PIPE_FORMAT_I8_UINT:
	case PIPE_FORMAT_I8_SINT:
	case PIPE_FORMAT_L8_UNORM:
	case PIPE_FORMAT_L8_SNORM:
	case PIPE_FORMAT_L8_UINT:
	case PIPE_FORMAT_L8_SINT:
	case PIPE_FORMAT_L8_SRGB:
	case PIPE_FORMAT_R8_UNORM:
	case PIPE_FORMAT_R8_SNORM:
	case PIPE_FORMAT_R8_UINT:
	case PIPE_FORMAT_R8_SINT:
		return TEX_FMT_8;

	/* 16-bit buffers. */
	case PIPE_FORMAT_B5G6R5_UNORM:
		return TEX_FMT_565;
	case PIPE_FORMAT_B5G5R5A1_UNORM:
	case PIPE_FORMAT_B5G5R5X1_UNORM:
		return TEX_FMT_1555;
	case PIPE_FORMAT_B4G4R4A4_UNORM:
	case PIPE_FORMAT_B4G4R4X4_UNORM:
		return TEX_FMT_4444;
	case PIPE_FORMAT_Z16_UNORM:
		return TEX_FMT_DEPTH16;
	case PIPE_FORMAT_L8A8_UNORM:
	case PIPE_FORMAT_L8A8_SNORM:
	case PIPE_FORMAT_L8A8_UINT:
	case PIPE_FORMAT_L8A8_SINT:
	case PIPE_FORMAT_L8A8_SRGB:
	case PIPE_FORMAT_R8G8_UNORM:
	case PIPE_FORMAT_R8G8_SNORM:
	case PIPE_FORMAT_R8G8_UINT:
	case PIPE_FORMAT_R8G8_SINT:
		return TEX_FMT_88;

	/* 32-bit buffers. */
	case PIPE_FORMAT_A8B8G8R8_SRGB:
	case PIPE_FORMAT_A8B8G8R8_UNORM:
	case PIPE_FORMAT_A8R8G8B8_UNORM:
	case PIPE_FORMAT_B8G8R8A8_SRGB:
	case PIPE_FORMAT_B8G8R8A8_UNORM:
	case PIPE_FORMAT_B8G8R8X8_UNORM:
	case PIPE_FORMAT_R8G8B8A8_SNORM:
	case PIPE_FORMAT_R8G8B8A8_UNORM:
	case PIPE_FORMAT_R8G8B8X8_UNORM:
	case PIPE_FORMAT_R8SG8SB8UX8U_NORM:
	case PIPE_FORMAT_X8B8G8R8_UNORM:
	case PIPE_FORMAT_X8R8G8B8_UNORM:
	case PIPE_FORMAT_R8G8B8_UNORM:
	case PIPE_FORMAT_R8G8B8A8_SINT:
	case PIPE_FORMAT_R8G8B8A8_UINT:
		return TEX_FMT_8888;

	/* YUV buffers. */
	case PIPE_FORMAT_UYVY:
		return TEX_FMT_UY1VY0;
	case PIPE_FORMAT_YUYV:
		return TEX_FMT_Y1UY0V;

	/* compressed formats */
	case PIPE_FORMAT_DXT1_RGB:
		return TEX_FMT_DXT1;

	default:
		return ~0;
	}
}

enum fgpf_color_mode
of_pipe2color(enum pipe_format format)
{
	switch (format) {
	/* 16-bit buffers. */
	case PIPE_FORMAT_B5G6R5_UNORM:
		return COLOR_RGB565;
	case PIPE_FORMAT_B5G5R5A1_UNORM:
		return COLOR_ARGB1555;
	case PIPE_FORMAT_B5G5R5X1_UNORM:
		return COLOR_RGB555;
	case PIPE_FORMAT_B4G4R4A4_UNORM:
	case PIPE_FORMAT_B4G4R4X4_UNORM:
		return COLOR_ARGB4444;

	/* 32-bit buffers. */
	case PIPE_FORMAT_B8G8R8X8_UNORM:
	case PIPE_FORMAT_R8G8B8X8_UNORM:
	case PIPE_FORMAT_R8SG8SB8UX8U_NORM:
	case PIPE_FORMAT_X8B8G8R8_UNORM:
	case PIPE_FORMAT_X8R8G8B8_UNORM:
	case PIPE_FORMAT_R8G8B8_UNORM:
		return COLOR_XRGB8888;
	case PIPE_FORMAT_A8B8G8R8_SRGB:
	case PIPE_FORMAT_A8B8G8R8_UNORM:
	case PIPE_FORMAT_A8R8G8B8_UNORM:
	case PIPE_FORMAT_B8G8R8A8_SRGB:
	case PIPE_FORMAT_B8G8R8A8_UNORM:
	case PIPE_FORMAT_R8G8B8A8_SNORM:
	case PIPE_FORMAT_R8G8B8A8_UNORM:
	case PIPE_FORMAT_R8G8B8A8_SINT:
	case PIPE_FORMAT_R8G8B8A8_UINT:
		return COLOR_ARGB8888;

	default:
		return ~0;
	}
}
#if 0
static inline enum sq_tex_swiz
tex_swiz(unsigned swiz)
{
	switch (swiz) {
	default:
	case PIPE_SWIZZLE_RED:   return SQ_TEX_X;
	case PIPE_SWIZZLE_GREEN: return SQ_TEX_Y;
	case PIPE_SWIZZLE_BLUE:  return SQ_TEX_Z;
	case PIPE_SWIZZLE_ALPHA: return SQ_TEX_W;
	case PIPE_SWIZZLE_ZERO:  return SQ_TEX_ZERO;
	case PIPE_SWIZZLE_ONE:   return SQ_TEX_ONE;
	}
}

uint32_t
of_tex_swiz(enum pipe_format format, unsigned swizzle_r, unsigned swizzle_g,
		unsigned swizzle_b, unsigned swizzle_a)
{
	const struct util_format_description *desc =
			util_format_description(format);
	uint8_t swiz[] = {
			swizzle_r, swizzle_g, swizzle_b, swizzle_a,
			PIPE_SWIZZLE_ZERO, PIPE_SWIZZLE_ONE,
			PIPE_SWIZZLE_ONE, PIPE_SWIZZLE_ONE,
	};

	return A2XX_SQ_TEX_3_SWIZ_X(tex_swiz(swiz[desc->swizzle[0]])) |
			A2XX_SQ_TEX_3_SWIZ_Y(tex_swiz(swiz[desc->swizzle[1]])) |
			A2XX_SQ_TEX_3_SWIZ_Z(tex_swiz(swiz[desc->swizzle[2]])) |
			A2XX_SQ_TEX_3_SWIZ_W(tex_swiz(swiz[desc->swizzle[3]]));
}
#endif
int
of_depth_supported(enum pipe_format format)
{
	switch (format) {
	case PIPE_FORMAT_Z24X8_UNORM:
	case PIPE_FORMAT_Z24_UNORM_S8_UINT:
	case PIPE_FORMAT_X8Z24_UNORM:
	case PIPE_FORMAT_S8_UINT_Z24_UNORM:
		return 1;
	default:
		return 0;
	}
}

enum fgpf_blend_factor
of_blend_factor(unsigned factor)
{
	switch (factor) {
	case PIPE_BLENDFACTOR_ONE:
		return BLEND_ONE;
	case PIPE_BLENDFACTOR_SRC_COLOR:
		return BLEND_SRC_COL;
	case PIPE_BLENDFACTOR_SRC_ALPHA:
		return BLEND_SRC_ALP;
	case PIPE_BLENDFACTOR_DST_ALPHA:
		return BLEND_DST_ALP;
	case PIPE_BLENDFACTOR_DST_COLOR:
		return BLEND_DST_COL;
	case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
		return BLEND_SRC_ALP_SAT;
	case PIPE_BLENDFACTOR_CONST_COLOR:
		return BLEND_CONST_COL;
	case PIPE_BLENDFACTOR_CONST_ALPHA:
		return BLEND_CONST_ALP;
	case PIPE_BLENDFACTOR_ZERO:
	case 0:
		return BLEND_ZERO;
	case PIPE_BLENDFACTOR_INV_SRC_COLOR:
		return BLEND_SRC_COL_INV;
	case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
		return BLEND_SRC_ALP_INV;
	case PIPE_BLENDFACTOR_INV_DST_ALPHA:
		return BLEND_DST_ALP_INV;
	case PIPE_BLENDFACTOR_INV_DST_COLOR:
		return BLEND_DST_COL_INV;
	case PIPE_BLENDFACTOR_INV_CONST_COLOR:
		return BLEND_CONST_COL_INV;
	case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
		return BLEND_CONST_ALP_INV;
	case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
	case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:
	case PIPE_BLENDFACTOR_SRC1_COLOR:
	case PIPE_BLENDFACTOR_SRC1_ALPHA:
		/* unsupported */
	default:
		DBG("invalid blend factor: %x", factor);
		return 0;
	}
}

enum fgpf_blend_op
of_blend_func(unsigned func)
{
	switch (func) {
	case PIPE_BLEND_ADD:
		return BLEND_SRC_ADD_DST;
	case PIPE_BLEND_MIN:
		return BLEND_MIN;
	case PIPE_BLEND_MAX:
		return BLEND_MAX;
	case PIPE_BLEND_SUBTRACT:
		return BLEND_SRC_SUB_DST;
	case PIPE_BLEND_REVERSE_SUBTRACT:
		return BLEND_DST_SUB_SRC;
	default:
		DBG("invalid blend func: %x", func);
		return 0;
	}
}
#if 0
enum adreno_pa_su_sc_draw
of_polygon_mode(unsigned mode)
{
	switch (mode) {
	case PIPE_POLYGON_MODE_POINT:
		return PC_DRAW_POINTS;
	case PIPE_POLYGON_MODE_LINE:
		return PC_DRAW_LINES;
	case PIPE_POLYGON_MODE_FILL:
		return PC_DRAW_TRIANGLES;
	default:
		DBG("invalid polygon mode: %u", mode);
		return 0;
	}
}
#endif
enum fgpf_stencil_action
of_stencil_op(unsigned op)
{
	switch (op) {
	case PIPE_STENCIL_OP_KEEP:
		return STENCIL_KEEP;
	case PIPE_STENCIL_OP_ZERO:
		return STENCIL_ZERO;
	case PIPE_STENCIL_OP_REPLACE:
		return STENCIL_REPLACE;
	case PIPE_STENCIL_OP_INCR:
		return STENCIL_INCR;
	case PIPE_STENCIL_OP_DECR:
		return STENCIL_DECR;
	case PIPE_STENCIL_OP_INCR_WRAP:
		return STENCIL_INCR_WRAP;
	case PIPE_STENCIL_OP_DECR_WRAP:
		return STENCIL_DECR_WRAP;
	case PIPE_STENCIL_OP_INVERT:
		return STENCIL_INVERT;
	default:
		DBG("invalid stencil op: %u", op);
		return 0;
	}
}
