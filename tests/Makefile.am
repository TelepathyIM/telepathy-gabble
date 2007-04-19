noinst_PROGRAMS = \
	test-gabble-presence \
	test-base64 \
	test-handles

python_tests = \
	test-basic-connect

python_tests_pythonfiles = $(python_tests:=.py)
python_tests_shfiles = $(python_tests:=.sh)

test_gabble_presence_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la

test_base64_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la

test_handles_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@ \
    @COVERAGE_CFLAGS@ @TP_GLIB_CFLAGS@ \
    -I $(top_srcdir)/src -I $(top_builddir)/src
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @LOUDMOUTH_LIBS@ @TP_GLIB_LIBS@

TESTS = $(noinst_PROGRAMS)
EXTRA_TESTS = $(python_tests_shfiles)

check-extra: $(BUILT_SOURCES)
	chmod +x run-with-tmp-session-bus.sh $(python_tests_shfiles) \
		$(python_tests_pythonfiles) exec-with-log.sh
	$(MAKE) check TESTS="$(EXTRA_TESTS)"

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
check_misc_sources = \
    $(python_tests_shfiles) \
    $(python_tests_pythonfiles)
include $(top_srcdir)/check-coding-style.mk
