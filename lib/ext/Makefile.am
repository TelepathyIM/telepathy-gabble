SUBDIRS =
DIST_SUBDIRS = wocky

all-local:
	@cd wocky && $(MAKE)

clean-local:
	if test -e wocky/Makefile ; then \
		cd wocky && $(MAKE) clean ; \
	fi

uninstall-local:
	@cd wocky/wocky && $(MAKE) uninstall

install-data-local:
	@cd wocky/wocky && $(MAKE) install
