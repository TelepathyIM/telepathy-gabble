noinst_PROGRAMS = \
	test-gabble-presence \
	test-base64 \
	test-handles

test_gabble_presence_SOURCES = \
    test-gabble-presence.c

test_gabble_presence_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la \
    $(top_builddir)/lib/telepathy-glib/libtelepathy-glib.la

test_base64_SOURCES = \
    test-base64.c

test_base64_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la \
    $(top_builddir)/lib/telepathy-glib/libtelepathy-glib.la

test_handles_SOURCES = \
    test-handles.c

test_handles_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la \
    $(top_builddir)/lib/telepathy-glib/libtelepathy-glib.la

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@ \
    @COVERAGE_CFLAGS@ \
    -I $(top_srcdir)/lib -I $(top_builddir)/lib \
    -I $(top_srcdir)/src -I $(top_builddir)/src
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @LOUDMOUTH_LIBS@

TESTS = $(noinst_PROGRAMS) test-basic-connect.sh

run-with-tmp-session-bus.conf: run-with-tmp-session-bus.conf.in Makefile
	@sed -e "s|\@abs_top_srcdir\@|@abs_top_srcdir@|" $< > $@
	
test-basic-connect.sh: run-with-tmp-session-bus.conf

# Dbus service file for testing
servicedir = .
service_in_files = org.freedesktop.Telepathy.ConnectionManager.gabble.service.in
service_DATA = $(service_in_files:.service.in=.service)

BUILT_FILES = $(service_DATA) run-with-tmp-session-bus.conf

CLEANFILES = $(BUILT_FILES)

# Rule to make the service file with builddir expanded
$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@abs_top_builddir\@|@abs_top_builddir@|" $< > $@

