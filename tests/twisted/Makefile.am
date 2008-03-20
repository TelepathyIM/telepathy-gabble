TESTS = \
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
	vcard/test-vcard-torture.py

_check:
	$(MAKE) check TESTS_ENVIRONMENT="PYTHONPATH=. python"

check-twisted:
	chmod +x run-with-tmp-session-bus.sh exec-with-log.sh
	sh run-with-tmp-session-bus.sh $(MAKE) _check

exec-with-log.sh: exec-with-log.sh.in
	sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@
	chmod +x $@

%.conf: %.conf.in
	sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@

%.service: %.service.in
	sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@

# D-Bus service file for testing
service_in_files = org.freedesktop.Telepathy.ConnectionManager.gabble.service.in
service_files = $(service_in_files:.service.in=.service)

# D-Bus config file for testing
conf_in_files = run-with-tmp-session-bus.conf.in
conf_files = $(conf_in_files:.conf.in=.conf)

BUILT_SOURCES = $(service_files) $(conf_files) exec-with-log.sh

EXTRA_DIST = \
	$(service_in_files) \
	$(conf_in_files) \
	exec-with-log.sh.in \
	$(TESTS)

CLEANFILES = gabble-[1-9]*.log

check_misc_sources = $(TESTS)

