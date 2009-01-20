if WANT_TWISTED_TESTS
CHECKTWISTED = twisted
endif
SUBDIRS = $(CHECKTWISTED)

noinst_PROGRAMS = \
	test-base64 \
	test-dtube-unique-names \
	test-handles \
	test-jid-decode \
	test-parse-message \
	test-presence

test_presence_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(ALL_LIBS)

test_base64_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(ALL_LIBS)

test_handles_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(ALL_LIBS)

test_jid_decode_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(ALL_LIBS)

test_parse_message_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(ALL_LIBS)

test_dtube_unique_names_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(ALL_LIBS)

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@ \
    @TP_GLIB_CFLAGS@ \
    -I $(top_srcdir) -I $(top_builddir)
ALL_LIBS = @DBUS_LIBS@ @GLIB_LIBS@ @LOUDMOUTH_LIBS@ @TP_GLIB_LIBS@

TESTS = $(noinst_PROGRAMS)

check-valgrind:
	G_SLICE=always-malloc \
	G_DEBUG=gc-friendly \
	$(MAKE) \
		TESTS_ENVIRONMENT="\
		libtool --mode=execute valgrind \
			--leak-check=full \
			--show-reachable=no \
			--error-exitcode=1" \
		check-TESTS

check_c_sources = \
	$(dbus_test_sources) \
	test-base64.c \
	test-dtube-unique-names.c \
	test-presence.c \
	test-jid-decode.c \
	test-handles.c \
	test-parse-message.c

include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style
