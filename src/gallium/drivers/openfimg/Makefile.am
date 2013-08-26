include $(top_srcdir)/src/gallium/Automake.inc

noinst_LTLIBRARIES = libopenfimg.la

AM_CFLAGS = \
	-Wno-packed-bitfield-compat \
	-I$(top_srcdir)/src/gallium/drivers \
	$(GALLIUM_CFLAGS) \
	$(OPENFIMG_CFLAGS) \
	$(VISIBILITY_CFLAGS)

libopenfimg_la_SOURCES = \
	openfimg_context.c \
	openfimg_draw.c \
	openfimg_fence.c \
	openfimg_resource.c \
	openfimg_screen.c \
	openfimg_state.c \
	openfimg_surface.c \
	openfimg_texture.c \
	openfimg_util.c
