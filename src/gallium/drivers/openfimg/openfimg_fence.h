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

#ifndef OPENFIMG_FENCE_H_
#define OPENFIMG_FENCE_H_

#include "util/u_inlines.h"
#include "util/u_double_list.h"


struct of_fence {
	int ref;
};

boolean of_fence_wait(struct of_fence *fence);
boolean of_fence_signalled(struct of_fence *fence);
void of_fence_del(struct of_fence *fence);

static INLINE void
of_fence_ref(struct of_fence *fence, struct of_fence **ref)
{
	if (fence)
		++fence->ref;

	if (*ref) {
		if (--(*ref)->ref == 0)
			of_fence_del(*ref);
	}

	*ref = fence;
}

static INLINE struct of_fence *
of_fence(struct pipe_fence_handle *fence)
{
	return (struct of_fence *)fence;
}


#endif /* OPENFIMG_FENCE_H_ */
