if HAVE_DBUS_TUBE
dbus_test_programs = test-dtube-unique-names
endif

if WANT_TWISTED_TESTS
CHECKTWISTED = twisted
endif
SUBDIRS = $(CHECKTWISTED)

noinst_PROGRAMS = \
	$(dbus_test_programs) \
	test-base64 \
	test-handles \
	test-jid-decode \
	test-parse-message \
	test-presence

test_presence_LDADD = \
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
    -I $(top_srcdir) -I $(top_builddir)
ALL_LIBS = @DBUS_LIBS@ @GLIB_LIBS@ @LOUDMOUTH_LIBS@ @TP_GLIB_LIBS@

TESTS = $(noinst_PROGRAMS)
TWISTED_TESTS = \
	olpc-muc-invitation.py \
	olpc-muc-prop-change.py \
	test-alias.py \
	test-alias-pep.py \
	test-alias-empty-vcard.py \
	test-avatar.py \
	test-avatar-async.py \
	test-avatar-tokens.py \
	test-capabilities.py \
	test-caps-update.py \
	test-connect.py \
	test-connect-fail.py \
	test-disco.py \
	test-google-roster.py \
	test-muc.py \
	test-muc-alias.py \
	test-muc-invitation.py \
	test-muc-ownership.py \
	test-muc-properties.py \
	test-muc-offer-stream-tube-ibb.py \
	test-muc-accept-stream-tube-ibb.py \
	test-muc-offer-dbus-tube.py \
	test-olpc-set-props-preload.py \
	test-register.py \
	test-roster.py \
	test-roster-subscribe.py \
	test-save-alias-to-vcard.py \
	test-set-alias.py \
	test-set-status-idempotence.py \
	test-si-tubes.py \
	test-text.py \
	test-text-delayed.py \
	test-text-no-body.py \
	test-vcard-cache.py \
	test-vcard-race.py \
	test-vcard-set-and-get.py \
	test-group-race.py \
	test-olpc-bundle.py

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
		check-TESTS

if HAVE_DBUS_TUBE
dbus_test_sources = test-dtube-unique-names.c
endif

check_c_sources = \
	$(dbus_test_sources) \
	test-base64.c \
	test-presence.c \
	test-jid-decode.c \
	test-handles.c \
	test-parse-message.c

include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style
