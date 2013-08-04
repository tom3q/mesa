if HAVE_GALLIUM_OPENFIMG

TARGET_DRIVERS += exynos
TARGET_CPPFLAGS += -DGALLIUM_OPENFIMG
TARGET_LIB_DEPS += \
	$(top_builddir)/src/gallium/winsys/openfimg/drm/libopenfimgdrm.la \
	$(top_builddir)/src/gallium/drivers/openfimg/libopenfimg.la \
	$(OPENFIMG_LIBS) \
	$(LIBDRM_LIBS)

endif
