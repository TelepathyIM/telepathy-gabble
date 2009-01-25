TWISTED_TESTS = \
	muc/name-conflict.py \
	muc/renamed.py \
	muc/roomlist.py \
	muc/send-error.py \
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
	olpc/view-presence.py \
	presence/presence.py \
	roster/ensure.py \
	roster/groups.py \
	roster/groups-12791.py \
	roster/request-group-before-roster.py \
	roster/request-group-after-roster.py \
	roster/test-google-roster.py \
	roster/test-roster.py \
	roster/test-roster-item-deletion.py \
	roster/test-roster-subscribe.py \
	roster/test-save-alias-to-roster.py \
	text/initiate.py \
	text/initiate-requestotron.py \
	text/destroy.py \
	text/ensure.py \
	text/respawn.py \
	text/send-error.py \
	text/test-chat-state.py \
	text/test-text-delayed.py \
	text/test-text-no-body.py \
	text/test-text.py \
	tubes/accept-muc-dbus-tube.py \
	tubes/accept-muc-stream-tube.py \
	tubes/accept-private-dbus-tube.py \
	tubes/accept-private-stream-tube.py \
	tubes/close-muc-with-closed-tube.py \
	tubes/crash-on-list-channels.py \
	tubes/create-invalid-tube-channels.py \
	tubes/ensure-si-tube.py \
	tubes/muc-presence.py \
	tubes/offer-muc-dbus-tube.py \
	tubes/offer-muc-stream-tube.py \
	tubes/offer-no-caps.py \
	tubes/offer-private-dbus-tube.py \
	tubes/offer-private-stream-tube.py \
	tubes/request-invalid-dbus-tube.py \
	tubes/test-get-available-tubes.py \
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
	jingle/accept-extra-stream.py \
	jingle/hold-audio.py \
	jingle/hold-av.py \
	jingle/stun-server.py \
	jingle/payload-types.py \
	jingle/stream-handler-error.py \
	jingle/test-content-adding-removal.py \
	jingle/test-description-info.py \
	jingle/test-incoming-call.py \
	jingle/test-incoming-call-reject.py \
	jingle/test-outgoing-call.py \
	jingle/test-outgoing-call-deprecated.py \
	jingle/test-outgoing-call-deprecated2.py \
	jingle/test-outgoing-call-requestotron.py \
	jingle/test-outgoing-call-rejected.py \
	jingle/test-outgoing-call-ensure.py \
	jingle/test-dialects-incoming.py \
	jingle/test-dialects-outgoing.py \
	jingle/test-content-complex.py \
	jingle/test-wait-for-caps.py \
	jingle/test-wait-for-caps-incomplete.py \
	test-capabilities.py \
	test-caps-cache.py \
	test-caps-hash.py \
	test-caps-tubes.py \
	caps_helper.py \
	connect/test-fail.py \
	connect/test-success.py \
	connect/test-twice.py \
	connect/test-nonblocking-tls.py \
	test-disco.py \
	test-disco-no-reply.py \
	test-register.py \
	test-set-status-idempotence.py \
        test-location.py

TESTS =

TESTS_ENVIRONMENT = \
	PYTHONPATH=@abs_top_srcdir@/tests/twisted:@abs_top_builddir@/tests/twisted

check-local: check-coding-style check-twisted

# set to 6 when using refdbg, to give Gabble time to exit
CHECK_TWISTED_SLEEP=0

check-twisted:
	$(MAKE) -C tools
	rm -f tools/core
	rm -f tools/gabble-testing.log
	if test -n "$$GABBLE_TEST_REFDBG"; then \
	  sleep=6; \
        else \
	  sleep=$(CHECK_TWISTED_SLEEP); \
	fi; \
	sh $(srcdir)/tools/with-session-bus.sh \
		--sleep=$$sleep \
		--config-file=tools/tmp-session-bus.conf \
		-- $(MAKE) check-TESTS \
		TESTS="$(TWISTED_TESTS)" \
		TESTS_ENVIRONMENT="$(TESTS_ENVIRONMENT) $(TEST_PYTHON)"
	@if test -e tools/core; then\
		echo "Core dump exists: tools/core";\
		exit 1;\
	fi

config.py: Makefile
	echo "PACKAGE_STRING = \"$(PACKAGE_STRING)\"" > config.py

BUILT_SOURCES = config.py

EXTRA_DIST = \
	$(TWISTED_TESTS) \
	bytestream.py \
	config.py \
	constants.py \
	gabbletest.py \
	servicetest.py \
	jingle/jingletest.py \
	jingle/jingletest2.py \
	mucutil.py \
	ns.py \
	olpc/util.py \
	tubes/muctubeutil.py \
	tubes/tubetestutil.py

noinst_PROGRAMS = \
	telepathy-gabble-debug

telepathy_gabble_debug_SOURCES = \
    main-debug.c \
    resolver-fake.c \
    resolver-fake.h

telepathy_gabble_debug_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la \
    $(top_builddir)/lib/gibber/libgibber.la \
    $(ALL_LIBS)

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@ \
    @TP_GLIB_CFLAGS@ \
    -I $(top_srcdir)/src -I $(top_builddir)/src \
    -I $(top_srcdir) -I $(top_builddir)
ALL_LIBS = @DBUS_LIBS@ @GLIB_LIBS@ @LOUDMOUTH_LIBS@ @TP_GLIB_LIBS@

CLEANFILES = gabble-[1-9]*.log *.pyc */*.pyc config.py

check_misc_sources = $(TESTS)

include $(top_srcdir)/tools/check-coding-style.mk

SUBDIRS = tools
