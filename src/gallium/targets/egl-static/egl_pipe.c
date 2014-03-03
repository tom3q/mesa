/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2011 LunarG Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */
#include "target-helpers/inline_debug_helper.h"
#include "target-helpers/inline_sw_helper.h"
#include "egl_pipe.h"

/* for openfimg */
#if _EGL_PIPE_OPENFIMG
#include "openfimg/drm/openfimg_drm_public.h"
#endif
static struct pipe_screen *
pipe_openfimg_create_screen(int fd)
{
#if _EGL_PIPE_FREEDRENO
   struct pipe_screen *screen;

   screen = of_drm_screen_create(fd);
   if (!screen)
      return NULL;

   screen = debug_screen_wrap(screen);

   return screen;
#else
   return NULL;
#endif
}

   else if (strcmp(name, "exynos") == 0)
      return pipe_openfimg_create_screen(fd);
struct pipe_screen *
egl_pipe_create_swrast_screen(struct sw_winsys *ws)
{
   struct pipe_screen *screen;

   screen = sw_screen_create(ws);
   if (screen)
      screen = debug_screen_wrap(screen);

   return screen;
}
