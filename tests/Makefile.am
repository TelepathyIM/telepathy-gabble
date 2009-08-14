if WANT_TWISTED_TESTS
CHECKTWISTED = twisted
endif
SUBDIRS = $(CHECKTWISTED)

noinst_PROGRAMS = \
	test-base64 \
	test-caps-hash \
	test-dtube-unique-names \
	test-gabble-idle-weak \
	test-handles \
	test-jid-decode \
	test-parse-message \
	test-presence

LDADD = $(top_builddir)/src/libgabble-convenience.la

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@ \
    @TP_GLIB_CFLAGS@ \
    -I $(top_srcdir) -I $(top_builddir)

TESTS = $(noinst_PROGRAMS)

TESTS_ENVIRONMENT = G_DEBUG=fatal-warnings,fatal-criticals

check-valgrind:
	G_SLICE=always-malloc \
	G_DEBUG=gc-friendly \
	$(MAKE) \
		TESTS_ENVIRONMENT="$(TESTS_ENVIRONMENT) \
		libtool --mode=execute valgrind \
			--leak-check=full \
			--show-reachable=no \
			--error-exitcode=1" \
		check-TESTS

check_c_sources = \
	$(dbus_test_sources) \
	test-base64.c \
	test-caps-hash.c \
	test-dtube-unique-names.c \
	test-presence.c \
	test-jid-decode.c \
	test-handles.c \
	test-parse-message.c

include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style
