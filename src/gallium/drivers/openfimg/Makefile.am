include Makefile.sources
include $(top_srcdir)/src/gallium/Automake.inc

noinst_LTLIBRARIES = libopenfimg.la

AM_CFLAGS = \
	-Wno-packed-bitfield-compat \
	-I$(top_srcdir)/src/gallium/drivers \
	$(GALLIUM_CFLAGS) \
	$(OPENFIMG_CFLAGS) \
	$(VISIBILITY_CFLAGS)

libopenfimg_la_SOURCES = $(C_SOURCES)
