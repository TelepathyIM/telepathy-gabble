if HAVE_DBUS_TUBE
dbus_test_programs = test-dtube-unique-names
endif

SUBDIRS = @CHECKTWISTED@

noinst_PROGRAMS = \
	$(dbus_test_programs) \
	test-gabble-presence \
	test-base64 \
	test-handles \
	test-jid-decode \
	test-parse-message

test_gabble_presence_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la $(ALL_LIBS)

test_base64_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la $(ALL_LIBS)

test_handles_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la $(ALL_LIBS)

test_jid_decode_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la $(ALL_LIBS)

test_parse_message_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la $(ALL_LIBS)

test_dtube_unique_names_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la $(ALL_LIBS)

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@ \
    @COVERAGE_CFLAGS@ @TP_GLIB_CFLAGS@ \
    -I $(top_srcdir)/src -I $(top_builddir)/src
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
		check

if HAVE_DBUS_TUBE
dbus_test_sources = test-dtube-unique-names.c
endif

check_c_sources = \
	$(dbus_test_sources) \
	test-base64.c \
	test-gabble-presence.c \
	test-jid-decode.c \
	test-handles.c \
	test-parse-message.c

include $(top_srcdir)/tools/check-coding-style.mk
