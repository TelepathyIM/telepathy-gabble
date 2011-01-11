SUBDIRS =
DIST_SUBDIRS = wocky telepathy-yell

all-local:
	@cd wocky && $(MAKE)
	@cd telepathy-yell && $(MAKE)

clean-local:
	if test -e wocky/Makefile ; then \
		cd wocky && $(MAKE) clean ; \
	fi

	if test -e telepathy-yell/Makefile ; then \
		cd telepathy-yell && $(MAKE) clean ; \
	fi

uninstall-local:
	@cd telepathy-yell/telepathy-yell && $(MAKE) uninstall

install-data-local:
	@cd telepathy-yell/telepathy-yell && $(MAKE) install
