LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

TELEPATHY_GABBLE_BUILT_SOURCES := \
	gabble/telepathy-gabble.pc \
	gabble/telepathy-gabble-uninstalled.pc \
	src/Android.mk \
	lib/gibber/Android.mk \
	lib/loudmouth/Android.mk \
	extensions/Android.mk

telepathy-gabble-configure-real:
	cd $(TELEPATHY_GABBLE_TOP) ; \
	CC="$(CONFIGURE_CC)" \
	CFLAGS="$(CONFIGURE_CFLAGS) -std=c99" \
	LD=$(TARGET_LD) \
	LDFLAGS="$(CONFIGURE_LDFLAGS)" \
	CPP=$(CONFIGURE_CPP) \
	CPPFLAGS="$(CONFIGURE_CPPFLAGS)" \
	PKG_CONFIG_LIBDIR=$(CONFIGURE_PKG_CONFIG_LIBDIR) \
	PKG_CONFIG_TOP_BUILD_DIR=$(PKG_CONFIG_TOP_BUILD_DIR) \
	$(TELEPATHY_GABBLE_TOP)/$(CONFIGURE) --host=arm-linux-androideabi \
	--disable-submodules \
		--disable-Werror --without-ca-certificates && \
	for file in $(TELEPATHY_GABBLE_BUILT_SOURCES); do \
		rm -f $$file && \
		make -C $$(dirname $$file) $$(basename $$file) ; \
	done

telepathy-gabble-configure: telepathy-gabble-configure-real

.PHONY: telepathy-gabble-configure

CONFIGURE_TARGETS += telepathy-gabble-configure

#include all the subdirs...
-include $(TELEPATHY_GABBLE_TOP)/src/Android.mk
-include $(TELEPATHY_GABBLE_TOP)/lib/gibber/Android.mk
-include $(TELEPATHY_GABBLE_TOP)/lib/loudmouth/Android.mk
-include $(TELEPATHY_GABBLE_TOP)/extensions/Android.mk
