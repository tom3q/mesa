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
#include "pipe/p_screen.h"
#include "pipe/p_state.h"

#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_format.h"
#include "util/u_format_s3tc.h"
#include "util/u_string.h"
#include "util/u_debug.h"

#include "os/os_time.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include "openfimg_screen.h"
//#include "openfimg_resource.h"
//#include "openfimg_fence.h"
#include "openfimg_util.h"

/* XXX this should go away */
#include "state_tracker/drm_driver.h"

static const struct debug_named_value debug_options[] = {
		{"msgs",      OF_DBG_MSGS,   "Print debug messages"},
		{"disasm",    OF_DBG_DISASM, "Dump TGSI and adreno shader disassembly"},
		{"dclear",    OF_DBG_DCLEAR, "Mark all state dirty after clear"},
		{"dgmem",     OF_DBG_DGMEM,  "Mark all state dirty after GMEM tile pass"},
		DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(of_mesa_debug, "OF_MESA_DEBUG", debug_options, 0)

int of_mesa_debug = 0;

static boolean
of_screen_is_format_supported(struct pipe_screen *pscreen,
		enum pipe_format format,
		enum pipe_texture_target target,
		unsigned sample_count,
		unsigned usage)
{
	unsigned retval = 0;

	if ((target >= PIPE_MAX_TEXTURE_TYPES) ||
			(sample_count > 1) || /* TODO add MSAA */
			!util_format_is_supported(format, usage)) {
		DBG("not supported: format=%s, target=%d, sample_count=%d, usage=%x",
				util_format_name(format), target, sample_count, usage);
		return FALSE;
	}

	/* TODO figure out how to render to other formats.. */
	if ((usage & PIPE_BIND_RENDER_TARGET) &&
			((format != PIPE_FORMAT_B8G8R8A8_UNORM) &&
			 (format != PIPE_FORMAT_B8G8R8X8_UNORM))) {
		DBG("not supported render target: format=%s, target=%d, sample_count=%d, usage=%x",
				util_format_name(format), target, sample_count, usage);
		return FALSE;
	}

	if ((usage & PIPE_BIND_SAMPLER_VIEW) &&
	    (of_pipe2texture(format) != ~0))
		retval |= PIPE_BIND_SAMPLER_VIEW;

	if ((usage & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET |
	    PIPE_BIND_SCANOUT | PIPE_BIND_SHARED)) &&
	    (of_pipe2color(format) != ~0)) {
		retval |= usage & (PIPE_BIND_RENDER_TARGET |
				PIPE_BIND_DISPLAY_TARGET |
				PIPE_BIND_SCANOUT |
				PIPE_BIND_SHARED);
	}

	if ((usage & PIPE_BIND_DEPTH_STENCIL) &&
			(of_depth_supported(format) != 0)) {
		retval |= PIPE_BIND_DEPTH_STENCIL;
	}

	if (usage & PIPE_BIND_VERTEX_BUFFER)
		retval |= PIPE_BIND_VERTEX_BUFFER;
	if (usage & PIPE_BIND_INDEX_BUFFER)
		retval |= PIPE_BIND_INDEX_BUFFER;
	if (usage & PIPE_BIND_TRANSFER_READ)
		retval |= PIPE_BIND_TRANSFER_READ;
	if (usage & PIPE_BIND_TRANSFER_WRITE)
		retval |= PIPE_BIND_TRANSFER_WRITE;

	if (retval != usage) {
		DBG("not supported: format=%s, target=%d, sample_count=%d, "
				"usage=%x, retval=%x", util_format_name(format),
				target, sample_count, usage, retval);
	}

	return retval == usage;
}

static const char *
of_screen_get_name(struct pipe_screen *pscreen)
{
	return "FIMG-3DSE";
}

static const char *
of_screen_get_vendor(struct pipe_screen *pscreen)
{
	return "OpenFIMG";
}

static uint64_t
of_screen_get_timestamp(struct pipe_screen *pscreen)
{
	int64_t cpu_time = os_time_get() * 1000;
	return cpu_time + of_screen(pscreen)->cpu_gpu_time_delta;
}

static void
of_screen_fence_ref(struct pipe_screen *pscreen,
		struct pipe_fence_handle **ptr,
		struct pipe_fence_handle *pfence)
{
	of_fence_ref(of_fence(pfence), (struct of_fence **)ptr);
}

static boolean
of_screen_fence_signalled(struct pipe_screen *screen,
		struct pipe_fence_handle *pfence)
{
	return of_fence_signalled(of_fence(pfence));
}

static boolean
of_screen_fence_finish(struct pipe_screen *screen,
		struct pipe_fence_handle *pfence,
		uint64_t timeout)
{
	return of_fence_wait(of_fence(pfence));
}

static void
of_screen_destroy(struct pipe_screen *pscreen)
{
	struct of_screen *screen = of_screen(pscreen);

	if (screen->pipe)
		of_pipe_del(screen->pipe);

	if (screen->dev)
		of_device_del(screen->dev);

	free(screen);
}

/* Verify all the following values. */
static int
of_screen_get_param(struct pipe_screen *pscreen, enum pipe_cap param)
{
	/* this is probably not totally correct.. but it's a start: */
	switch (param) {
	/* Supported features (boolean caps). */
	case PIPE_CAP_NPOT_TEXTURES:
	case PIPE_CAP_TWO_SIDED_STENCIL:
	case PIPE_CAP_POINT_SPRITE:
	case PIPE_CAP_TEXTURE_MIRROR_CLAMP:
	case PIPE_CAP_BLEND_EQUATION_SEPARATE:
	case PIPE_CAP_TEXTURE_SWIZZLE:
	case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
	case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
	case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
	case PIPE_CAP_SM3:
	case PIPE_CAP_SEAMLESS_CUBE_MAP:
	case PIPE_CAP_CONDITIONAL_RENDER:
	case PIPE_CAP_TEXTURE_BARRIER:
	case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
	case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
	case PIPE_CAP_TGSI_INSTANCEID:
	case PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY:
	case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
	case PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY:
	case PIPE_CAP_USER_CONSTANT_BUFFERS:
	case PIPE_CAP_USER_INDEX_BUFFERS:
	case PIPE_CAP_USER_VERTEX_BUFFERS:
		return 1;

	/* Unsupported features. */
	case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
	case PIPE_CAP_ANISOTROPIC_FILTER:
	case PIPE_CAP_COMPUTE:
	case PIPE_CAP_MIXED_COLORBUFFER_FORMATS:
	case PIPE_CAP_PRIMITIVE_RESTART:
	case PIPE_CAP_SHADER_STENCIL_EXPORT:
	case PIPE_CAP_START_INSTANCE:
	case PIPE_CAP_TEXTURE_MULTISAMPLE:
	case PIPE_CAP_TEXTURE_SHADOW_MAP:
	case PIPE_CAP_INDEP_BLEND_ENABLE:
	case PIPE_CAP_INDEP_BLEND_FUNC:
	case PIPE_CAP_DEPTH_CLIP_DISABLE:
	case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
	case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
	case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
	case PIPE_CAP_SCALED_RESOLVE:
	case PIPE_CAP_TGSI_CAN_COMPACT_CONSTANTS:
	case PIPE_CAP_FRAGMENT_COLOR_CLAMPED:
	case PIPE_CAP_VERTEX_COLOR_CLAMPED:
	case PIPE_CAP_QUERY_PIPELINE_STATISTICS:
	case PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK:
		return 0;

	case PIPE_CAP_TGSI_TEXCOORD:
	case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
		return 0;

	case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
		return 16;

	case PIPE_CAP_GLSL_FEATURE_LEVEL:
		return 120;

	/* Stream output. */
	case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
	case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
	case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
	case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
		return 0;

	/* Texturing. */
	case PIPE_CAP_MAX_TEXTURE_2D_LEVELS:
	case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
		return 12;
	case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
		return 0;
	case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
		return 0;
	case PIPE_CAP_MAX_COMBINED_SAMPLERS:
		return 12;

	/* Render targets. */
	case PIPE_CAP_MAX_RENDER_TARGETS:
		return 1;

	/* Timer queries. */
	case PIPE_CAP_QUERY_TIME_ELAPSED:
	case PIPE_CAP_OCCLUSION_QUERY:
	case PIPE_CAP_QUERY_TIMESTAMP:
		return 0;

	case PIPE_CAP_MIN_TEXEL_OFFSET:
	case PIPE_CAP_MAX_TEXEL_OFFSET:
		return 0;

	case PIPE_CAP_ENDIANNESS:
		return PIPE_ENDIAN_LITTLE;

	default:
		DBG("unknown param %d", param);
		return 0;
	}
}

static float
of_screen_get_paramf(struct pipe_screen *pscreen, enum pipe_capf param)
{
	switch (param) {
	case PIPE_CAPF_MAX_LINE_WIDTH:
	case PIPE_CAPF_MAX_LINE_WIDTH_AA:
		return 128.0f;
	case PIPE_CAPF_MAX_POINT_WIDTH:
	case PIPE_CAPF_MAX_POINT_WIDTH_AA:
		return 2048.0f;
	case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
		return 0.0f;
	case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
		return 0.0f;
	case PIPE_CAPF_GUARD_BAND_LEFT:
	case PIPE_CAPF_GUARD_BAND_TOP:
	case PIPE_CAPF_GUARD_BAND_RIGHT:
	case PIPE_CAPF_GUARD_BAND_BOTTOM:
		return 0.0f;
	default:
		DBG("unknown paramf %d", param);
		return 0;
	}
}

static int
of_screen_get_shader_param(struct pipe_screen *pscreen, unsigned shader,
		enum pipe_shader_cap param)
{
	switch (shader) {
	case PIPE_SHADER_FRAGMENT:
	case PIPE_SHADER_VERTEX:
		break;
	case PIPE_SHADER_COMPUTE:
	case PIPE_SHADER_GEOMETRY:
		/* maye we could emulate.. */
		return 0;
	default:
		DBG("unknown shader type %d", shader);
		return 0;
	}

	/* this is probably not totally correct.. but it's a start: */
	switch (param) {
	case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
	case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
	case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
	case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
		return 512;
	case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
		return 8; /* XXX */
	case PIPE_SHADER_CAP_MAX_INPUTS:
		return 32;
	case PIPE_SHADER_CAP_MAX_TEMPS:
		return 32; /* Max native temporaries. */
	case PIPE_SHADER_CAP_MAX_ADDRS:
		return 4; /* Max native address registers */
	case PIPE_SHADER_CAP_MAX_CONSTS:
		return 1024;
	case case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
		return 0;
	case PIPE_SHADER_CAP_MAX_PREDS:
		return 7; /* nothing uses this */
	case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
		return 0;
	case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
	case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
	case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
	case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
		return 1;
	case PIPE_SHADER_CAP_SUBROUTINES:
		return 1;
	case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
	case PIPE_SHADER_CAP_INTEGERS:
		return 0;
	case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
		if (shader == PIPE_SHADER_VERTEX)
			return 4;
		return 8;
	case PIPE_SHADER_CAP_PREFERRED_IR:
		return PIPE_SHADER_IR_TGSI;
	default:
		DBG("unknown shader param %d", param);
		return 0;
	}
	return 0;
}

boolean
of_screen_bo_get_handle(struct pipe_screen *pscreen,
		struct of_bo *bo,
		unsigned stride,
		struct winsys_handle *whandle)
{
	whandle->stride = stride;

	if (whandle->type == DRM_API_HANDLE_TYPE_SHARED) {
		return of_bo_get_name(bo, &whandle->handle) == 0;
	} else if (whandle->type == DRM_API_HANDLE_TYPE_KMS) {
		whandle->handle = of_bo_handle(bo);
		return TRUE;
	} else {
		return FALSE;
	}
}

struct of_bo *
of_screen_bo_from_handle(struct pipe_screen *pscreen,
		struct winsys_handle *whandle,
		unsigned *out_stride)
{
	struct of_screen *screen = of_screen(pscreen);
	struct of_bo *bo;

	bo = of_bo_from_name(screen->dev, whandle->handle);
	if (!bo) {
		DBG("ref name 0x%08x failed", whandle->handle);
		return NULL;
	}

	*out_stride = whandle->stride;

	return bo;
}

struct pipe_screen *
of_screen_create(struct of_device *dev)
{
	struct of_screen *screen = CALLOC_STRUCT(of_screen);
	struct pipe_screen *pscreen;
	uint64_t val;

	of_mesa_debug = debug_get_option_of_mesa_debug();

	if (!screen)
		return NULL;

	pscreen = &screen->base;

	screen->dev = dev;

	// maybe this should be in context?
	screen->pipe = of_pipe_new(screen->dev, OF_PIPE_3D);
	if (!screen->pipe) {
		DBG("could not create 3d pipe");
		goto fail;
	}

	if (of_pipe_get_param(screen->pipe, OF_GMEM_SIZE, &val)) {
		DBG("could not get GMEM size");
		goto fail;
	}
	screen->gmemsize_bytes = val;

	if (of_pipe_get_param(screen->pipe, OF_DEVICE_ID, &val)) {
		DBG("could not get device-id");
		goto fail;
	}
	screen->device_id = val;

	if (of_pipe_get_param(screen->pipe, OF_GPU_ID, &val)) {
		DBG("could not get gpu-id");
		goto fail;
	}
	screen->gpu_id = val;

	pscreen->context_create = of_context_create;
	pscreen->is_format_supported = of_screen_is_format_supported;

	pscreen->destroy = of_screen_destroy;
	pscreen->get_param = of_screen_get_param;
	pscreen->get_paramf = of_screen_get_paramf;
	pscreen->get_shader_param = of_screen_get_shader_param;

	of_resource_screen_init(pscreen);

	pscreen->get_name = of_screen_get_name;
	pscreen->get_vendor = of_screen_get_vendor;

	pscreen->get_timestamp = of_screen_get_timestamp;

	pscreen->fence_reference = of_screen_fence_ref;
	pscreen->fence_signalled = of_screen_fence_signalled;
	pscreen->fence_finish = of_screen_fence_finish;

	util_format_s3tc_init();

	return pscreen;

fail:
	of_screen_destroy(pscreen);
	return NULL;
}
