SUBDIRS = twisted suppressions

tests_list = \
	test-base64 \
	test-dtube-unique-names \
	test-gabble-idle-weak \
	test-handles \
	test-jid-decode \
	test-parse-message \
	test-presence \
	test-tp-error-from-wocky

gabble-C-tests.list:
	$(AM_V_GEN)echo $(tests_list) > $@

if ENABLE_INSTALLED_TESTS
gabbletests_PROGRAMS = $(tests_list)
gabbletests_DATA = gabble-C-tests.list
else
noinst_PROGRAMS = $(tests_list)
endif

LDADD = $(top_builddir)/src/libgabble-convenience.la

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @WOCKY_CFLAGS@ \
    @TP_GLIB_CFLAGS@ \
    -I $(top_srcdir) -I $(top_builddir) \
    -I $(top_srcdir)/lib -I $(top_builddir)/lib

TESTS = $(tests_list)

TESTS_ENVIRONMENT = \
  abs_top_builddir=@abs_top_builddir@ \
  G_DEBUG=fatal-warnings,fatal-criticals \
  sh $(top_srcdir)/tests/twisted/tools/with-session-bus.sh \
    --config-file=$(top_builddir)/tests/twisted/tools/servicedir-uninstalled/tmp-session-bus.conf --


check-valgrind: $(TESTS)
	G_SLICE=always-malloc \
	G_DEBUG=gc-friendly \
	$(MAKE) \
		TESTS_ENVIRONMENT="$(TESTS_ENVIRONMENT) \
		libtool --mode=execute valgrind \
			--leak-check=full \
			--show-reachable=no \
			--gen-suppressions=all \
			--num-callers=20 \
			--suppressions=@abs_top_srcdir@/tests/suppressions/gabble.supp \
			--suppressions=@abs_top_srcdir@/tests/suppressions/tp-glib.supp \
			--error-exitcode=1" \
		check-TESTS

check_c_sources = \
	$(dbus_test_sources) \
	test-base64.c \
	test-dtube-unique-names.c \
	test-presence.c \
	test-jid-decode.c \
	test-handles.c \
	test-parse-message.c \
	tp-error-from-wocky.c

test_tp_error_from_wocky_SOURCES = tp-error-from-wocky.c

include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style
