TWISTED_TESTS = \
	muc/roomlist.py \
	muc/test-ensure.py \
	muc/test-muc-alias.py \
	muc/test-muc-invitation.py \
	muc/test-muc-ownership.py \
	muc/test-muc-properties.py \
	muc/test-muc.py \
	olpc/olpc-activity-search.py \
	olpc/current-activity.py \
	olpc/olpc-muc-invitation.py \
	olpc/olpc-muc-prop-change.py \
	olpc/test-olpc-bundle.py \
	olpc/test-olpc-set-props-preload.py \
	olpc/change-notifications.py \
	olpc/olpc-buddy-search.py \
	olpc/gadget-invite.py \
	olpc/gadget-publish.py \
	olpc/gadget-restarted.py \
	presence/presence.py \
	roster/ensure.py \
	roster/groups.py \
	roster/groups-12791.py \
	roster/request-group-before-roster.py \
	roster/request-group-after-roster.py \
	roster/test-google-roster.py \
	roster/test-roster.py \
	roster/test-roster-subscribe.py \
	roster/test-save-alias-to-roster.py \
	text/initiate.py \
	text/initiate-requestotron.py \
	text/ensure.py \
	text/respawn.py \
	text/test-text-delayed.py \
	text/test-text-no-body.py \
	text/test-text.py \
	tubes/test-get-available-tubes.py \
	tubes/test-muc-accept-stream-tube-ibb.py \
	tubes/test-muc-offer-dbus-tube.py \
	tubes/test-muc-offer-stream-tube-ibb.py \
	tubes/test-si-tubes.py \
	tubes/ensure-si-tube.py \
	vcard/test-alias-empty-vcard.py \
	vcard/test-alias-pep.py \
	vcard/test-alias.py \
	vcard/test-avatar-async.py \
	vcard/test-avatar.py \
	vcard/test-avatar-retrieved.py \
	vcard/test-avatar-tokens.py \
	vcard/test-muc-alias.py \
	vcard/test-save-alias-to-vcard.py \
	vcard/test-set-alias.py \
	vcard/test-vcard-cache.py \
	vcard/test-vcard-race.py \
	vcard/test-vcard-set-and-get.py \
	jingle/hold-audio.py \
	jingle/hold-av.py \
	jingle/test-content-adding-removal.py \
	jingle/test-incoming-call.py \
	jingle/test-incoming-call-reject.py \
	jingle/test-outgoing-call.py \
	jingle/test-outgoing-call-deprecated.py \
	jingle/test-outgoing-call-deprecated2.py \
	jingle/test-outgoing-call-requestotron.py \
	jingle/test-outgoing-call-rejected.py \
	jingle/test-outgoing-call-ensure.py \
	test-capabilities.py \
	test-caps-cache.py \
	test-caps-hash.py \
	connect/test-fail.py \
	connect/test-success.py \
	connect/test-twice.py \
	connect/test-nonblocking-tls.py \
	test-disco.py \
	test-disco-no-reply.py \
	test-register.py \
	test-set-status-idempotence.py

TESTS =

TESTS_ENVIRONMENT = \
	PYTHONPATH=@abs_top_srcdir@/tests/twisted:@abs_top_builddir@/tests/twisted

check-local: check-coding-style check-twisted

check-twisted:
	$(MAKE) -C tools
	rm -f tools/core
	rm -f tools/gabble-testing.log
	sh $(srcdir)/tools/with-session-bus.sh --config-file=tools/tmp-session-bus.conf -- $(MAKE) check-TESTS \
		TESTS="$(TWISTED_TESTS)" \
		TESTS_ENVIRONMENT="$(TESTS_ENVIRONMENT) $(TEST_PYTHON)"
	@if test -e tools/core; then\
		echo "Core dump exists: tools/core";\
		exit 1;\
	fi

EXTRA_DIST = \
	$(TWISTED_TESTS) \
	gabbletest.py \
	servicetest.py \
	jingle/jingletest.py \
	ns.py \
	olpc/util.py

noinst_PROGRAMS = \
	telepathy-gabble-debug

telepathy_gabble_debug_SOURCES = \
    main-debug.c

telepathy_gabble_debug_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la $(ALL_LIBS)

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@ \
    @COVERAGE_CFLAGS@ @TP_GLIB_CFLAGS@ \
    -I $(top_srcdir)/src -I $(top_builddir)/src
ALL_LIBS = @DBUS_LIBS@ @GLIB_LIBS@ @LOUDMOUTH_LIBS@ @TP_GLIB_LIBS@

CLEANFILES = gabble-[1-9]*.log *.pyc */*.pyc

check_misc_sources = $(TESTS)

include $(top_srcdir)/tools/check-coding-style.mk

SUBDIRS = tools
