noinst_PROGRAMS = \
	test-gabble-presence \
	test-base64 \
	test-handles \
	test-jid-decode \
	test-parse-message

test_gabble_presence_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la

test_base64_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la

test_handles_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la

test_jid_decode_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la

test_parse_message_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@ \
    @COVERAGE_CFLAGS@ @TP_GLIB_CFLAGS@ \
    -I $(top_srcdir)/src -I $(top_builddir)/src
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @LOUDMOUTH_LIBS@ @TP_GLIB_LIBS@

TESTS = $(noinst_PROGRAMS)
TWISTED_TESTS = \
	test-avatar.py \
	test-avatar-async.py \
	test-connect.py \
	test-connect-fail.py \
	test-disco.py \
	test-muc.py \
	test-muc-ownership.py \
	test-muc-properties.py \
	test-register.py \
	test-roster.py \
	test-roster-subscribe.py \
	test-save-alias-to-vcard.py \
	test-olpc-set-props-preload.py \
	test-set-status-idempotence.py \
	test-text.py \
	test-text-delayed.py \
	test-vcard-race.py

_check-twisted:
	$(MAKE) check TESTS="$(TWISTED_TESTS)" TESTS_ENVIRONMENT=python

check-twisted:
	chmod +x run-with-tmp-session-bus.sh exec-with-log.sh
	sh run-with-tmp-session-bus.sh $(MAKE) _check-twisted

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

exec-with-log.sh: exec-with-log.sh.in
	@sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@

%.conf: %.conf.in
	@sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@

%.service: %.service.in
	@sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@

# D-Bus service file for testing
service_in_files = org.freedesktop.Telepathy.ConnectionManager.gabble.service.in
service_files = $(service_in_files:.service.in=.service)

# D-Bus config file for testing
conf_in_files = run-with-tmp-session-bus.conf.in
conf_files = $(conf_in_files:.conf.in=.conf)

BUILT_SOURCES = $(service_files) $(conf_files) exec-with-log.sh

EXTRA_DIST = $(service_in_files) $(conf_in_files) exec-with-log.sh.in

CLEANFILES = $(BUILT_SOURCES) gabble-[1-9]*.log

check_c_sources = *.c
check_misc_sources = $(TWISTED_TESTS)
include $(top_srcdir)/check-coding-style.mk
