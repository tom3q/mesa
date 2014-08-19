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

#ifndef OPENFIMG_RESOURCE_H_
#define OPENFIMG_RESOURCE_H_

#include "util/u_transfer.h"

#include "openfimg_util.h"

struct of_resource_slice {
	uint32_t offset;         /* offset of first layer in slice */
	uint32_t pixoffset;
	uint32_t pitch;
	uint32_t size0;          /* size of first layer in slice */
};

struct of_resource {
	struct u_resource base;
	struct fd_bo *bo;
	uint32_t cpp;
	struct of_resource_slice slices[MAX_MIP_LEVELS];
	uint32_t timestamp;
	bool dirty;
};

static INLINE struct of_resource *
of_resource(struct pipe_resource *ptex)
{
	return (struct of_resource *)ptex;
}

static INLINE struct of_resource_slice *
of_resource_slice(struct of_resource *rsc, unsigned level)
{
	assert(level <= rsc->base.b.last_level);
	return &rsc->slices[level];
}

void of_resource_screen_init(struct pipe_screen *pscreen);
void of_resource_context_init(struct pipe_context *pctx);

#endif /* OPENFIMG_RESOURCE_H_ */
