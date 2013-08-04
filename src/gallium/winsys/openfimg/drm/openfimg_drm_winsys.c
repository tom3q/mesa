#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/u_format.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"

#include "openfimg_drm_public.h"

#include "openfimg/openfimg_screen.h"

struct pipe_screen *
of_drm_screen_create(int fd)
{
	struct of_device *dev = of_device_new(fd);
	if (!dev)
		return NULL;
	return of_screen_create(dev);
}
