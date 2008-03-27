TWISTED_TESTS = \
	muc/test-muc-invitation.py \
	muc/test-muc-ownership.py \
	muc/test-muc-properties.py \
	muc/test-muc.py \
	olpc/olpc-muc-invitation.py \
	olpc/olpc-muc-prop-change.py \
	olpc/test-olpc-bundle.py \
	olpc/test-olpc-set-props-preload.py \
	roster/test-google-roster.py \
	roster/test-roster.py \
	roster/test-roster-subscribe.py \
	text/test-text-delayed.py \
	text/test-text-no-body.py \
	text/test-text.py \
	tubes/test-get-available-tubes.py \
	tubes/test-muc-accept-stream-tube-ibb.py \
	tubes/test-muc-offer-dbus-tube.py \
	tubes/test-muc-offer-stream-tube-ibb.py \
	tubes/test-si-tubes.py \
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
	vcard/test-vcard-torture.py \
	jingle/test-incoming-call.py \
	jingle/test-outgoing-call.py \
	jingle/test-outgoing-call-rejected.py

TESTS = check-twisted

TESTS_ENVIRONMENT = \
	PYTHONPATH=@abs_top_srcdir@/tests/twisted

check-twisted:
	$(MAKE) -C tools
	sh tools/with-session-bus.sh --config-file=tools/tmp-session-bus.conf -- $(MAKE) check \
		TESTS="$(TWISTED_TESTS)" TESTS_ENVIRONMENT="PYTHONPATH=@abs_top_srcdir@/tests/twisted $(PYTHON)"

EXTRA_DIST = \
	$(TESTS)

CLEANFILES = gabble-[1-9]*.log *.pyc */*.pyc

check_misc_sources = $(TESTS)

include $(top_srcdir)/tools/check-coding-style.mk
