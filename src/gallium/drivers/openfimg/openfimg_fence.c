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

#include "util/u_memory.h"

#include "openfimg_context.h"
#include "openfimg_fence.h"
#include "openfimg_util.h"

boolean
of_fence_wait(struct of_fence *fence)
{
	DBG("fence = %p", fence);
	//return !fd_pipe_wait(fence->pipe, fence->timestamp);
	return false;
}

boolean
of_fence_signalled(struct of_fence *fence)
{
	DBG("TODO: fence = %p", fence);
	return false;
}

void
of_fence_del(struct of_fence *fence)
{
	FREE(fence);
}

void of_fence_new(struct of_context *ctx, uint32_t timestamp,
		  struct of_fence **fence)
{
	struct of_fence *of_fence = CALLOC_STRUCT(of_fence);

	if (of_fence) {
		of_fence->pipe = ctx->pipe;
		of_fence->timestamp = timestamp;
	}

	of_fence_ref(of_fence, fence);
}
