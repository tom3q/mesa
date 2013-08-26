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

#ifndef OPENFIMG_SCREEN_H_
#define OPENFIMG_SCREEN_H_

#include <openfimg_drmif.h>
#include <openfimg_ringbuffer.h>

#include "pipe/p_screen.h"
#include "util/u_memory.h"

typedef uint32_t u32;

struct of_bo;

struct of_screen {
	struct pipe_screen base;

	struct of_device *dev;
	struct of_pipe *pipe;

	int64_t cpu_gpu_time_delta;
};

static INLINE struct of_screen *
of_screen(struct pipe_screen *pscreen)
{
	return (struct of_screen *)pscreen;
}

boolean of_screen_bo_get_handle(struct pipe_screen *pscreen,
		struct of_bo *bo,
		unsigned stride,
		struct winsys_handle *whandle);
struct of_bo * of_screen_bo_from_handle(struct pipe_screen *pscreen,
		struct winsys_handle *whandle,
		unsigned *out_stride);

struct pipe_screen * of_screen_create(struct of_device *dev);

#endif /* OPENFIMG_SCREEN_H_ */
