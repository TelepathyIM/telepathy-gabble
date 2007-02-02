noinst_PROGRAMS = \
	test-gabble-presence \
	test-base64 \
	test-handles \
	test-handle-set

test_intset_SOURCES = \
    test-intset.c

test_intset_LDADD = \
    $(top_builddir)/lib/libtelepathy-glib.la

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

TESTS = $(noinst_PROGRAMS) $(python_tests_shfiles)

run-with-tmp-session-bus.conf: run-with-tmp-session-bus.conf.in Makefile
	@sed -e "s|\@abs_top_builddir\@|@abs_top_builddir@|" $< > $@
	
test-basic-connect.sh: run-with-tmp-session-bus.conf

# Dbus service file for testing
service_in_files = org.freedesktop.Telepathy.ConnectionManager.gabble.service.in
service_files = $(service_in_files:.service.in=.service)

BUILT_FILES = $(service_files) run-with-tmp-session-bus.conf

EXTRA_DIST = $(service_in_files) $(service_files) run-with-tmp-session-bus.conf.in run-with-tmp-session-bus.sh dbustesting.py $(python_tests_pythonfiles) $(python_tests_shfiles)

CLEANFILES = $(BUILT_FILES)

# Rule to make the service file with builddir expanded
$(service_files): $(service_in_files) Makefile
	@sed -e "s|\@abs_top_builddir\@|@abs_top_builddir@|" $< > $@

## copy tests to builddir so that generated tests and static tests 
## are all in one place.
all-local: $(service_files)
	if ! (test $(srcdir) = . || test $(srcdir) -ef .) ; then								\
		FILES='$(python_tests_pythonfiles) $(python_tests_shfiles)' ;					\
	        for F in $$FILES; do								\
			SRC=$(srcdir)/$$F ;							\
			DEST=$(top_builddir)/tests/$$F ;						\
	                echo '-- Copying python test file '$$F ;					\
			cp -f $$SRC $$DEST || exit 1 ;						\
		done ;										\
		echo '-- Copying' $(top_srcdir)/tests/run-with-tmp-session-bus.sh 'to $(top_builddir)/tests directory' ;	\
		cp -f $(top_srcdir)/tests/run-with-tmp-session-bus.sh $(top_builddir)/tests || exit 1 ;  \
		echo '-- Copying' $(top_srcdir)/tests/dbustesting.py 'to $(top_builddir)/tests directory' ;	\
		cp -f $(top_srcdir)/tests/dbustesting.py $(top_builddir)/tests || exit 1 ;  \
	else											\
		echo '-- No need to copy test data as srcdir = builddir' ;			\
	fi ;											
	
#	chmod u+w $(top_builddir)/test/data/valid-config-files/*.conf || exit 1

## this doesn't clean generated test data files when srcdir=builddir
clean-local:
	if test $(srcdir) != . ; then                                   \
		FILES='$(python_tests_pythonfiles) $(python_tests_shfiles) run-with-tmp-session-bus.sh dbustesting.py dbustesting.pyc' ;					\
                for F in $$FILES; do                                    \
                        DEST=$(top_builddir)/tests/$$F ;                 \
                        echo '-- Deleting test file '$$F ;              \
                        rm -f $$DEST || exit 1 ;                           \
                done ;                                                  \
        fi
