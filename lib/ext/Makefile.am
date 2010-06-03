SUBDIRS =
DIST_SUBDIRS = wocky

all-local:
	@cd wocky && $(MAKE)

clean-local:
	if test -e wocky/Makefile ; then \
		cd wocky && $(MAKE) clean ; \
	fi

install-data-local:
	@cd wocky && $(MAKE) install
