noinst_PROGRAMS = \
	test-gabble-presence \
	test-base64 \
	test-handles

python_tests = \
	test-basic-connect.

python_tests_pythonfiles = $(python_tests:.=.py)
python_tests_shfiles = $(python_tests:.=.sh)

test_gabble_presence_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la \
    $(top_builddir)/lib/telepathy-glib/libtelepathy-glib.la

test_base64_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la \
    $(top_builddir)/lib/telepathy-glib/libtelepathy-glib.la

test_handles_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la \
    $(top_builddir)/lib/telepathy-glib/libtelepathy-glib.la

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@ \
    @COVERAGE_CFLAGS@ \
    -I $(top_srcdir)/lib -I $(top_builddir)/lib \
    -I $(top_srcdir)/src -I $(top_builddir)/src
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @LOUDMOUTH_LIBS@

TESTS = $(noinst_PROGRAMS)
TESTSEXTRA = $(python_tests_shfiles)

check-extra: $(service_files)
	@failed=0; all=0; xfail=0; xpass=0; skip=0; \
	srcdir=$(srcdir); export srcdir; \
	list='$(TESTSEXTRA)'; \
	if test -n "$$list"; then \
	  for tst in $$list; do \
	    if test -f ./$$tst; then dir=./; \
	    elif test -f $$tst; then dir=; \
	    else dir="$(srcdir)/"; fi; \
	    if $(TESTS_ENVIRONMENT) $${dir}$$tst; then \
	      all=`expr $$all + 1`; \
	      case " $(XFAIL_TESTS) " in \
	      *" $$tst "*) \
		xpass=`expr $$xpass + 1`; \
		failed=`expr $$failed + 1`; \
		echo "XPASS: $$tst"; \
	      ;; \
	      *) \
		echo "PASS: $$tst"; \
	      ;; \
	      esac; \
	    elif test $$? -ne 77; then \
	      all=`expr $$all + 1`; \
	      case " $(XFAIL_TESTS) " in \
	      *" $$tst "*) \
		xfail=`expr $$xfail + 1`; \
		echo "XFAIL: $$tst"; \
	      ;; \
	      *) \
		failed=`expr $$failed + 1`; \
		echo "FAIL: $$tst"; \
	      ;; \
	      esac; \
	    else \
	      skip=`expr $$skip + 1`; \
	      echo "SKIP: $$tst"; \
	    fi; \
	  done; \
	  if test "$$failed" -eq 0; then \
	    if test "$$xfail" -eq 0; then \
	      banner="All $$all tests passed"; \
	    else \
	      banner="All $$all tests behaved as expected ($$xfail expected failures)"; \
	    fi; \
	  else \
	    if test "$$xpass" -eq 0; then \
	      banner="$$failed of $$all tests failed"; \
	    else \
	      banner="$$failed of $$all tests did not behave as expected ($$xpass unexpected passes)"; \
	    fi; \
	  fi; \
	  dashes="$$banner"; \
	  skipped=""; \
	  if test "$$skip" -ne 0; then \
	    skipped="($$skip tests were not run)"; \
	    test `echo "$$skipped" | wc -c` -le `echo "$$banner" | wc -c` || \
	      dashes="$$skipped"; \
	  fi; \
	  report=""; \
	  if test "$$failed" -ne 0 && test -n "$(PACKAGE_BUGREPORT)"; then \
	    report="Please report to $(PACKAGE_BUGREPORT)"; \
	    test `echo "$$report" | wc -c` -le `echo "$$banner" | wc -c` || \
	      dashes="$$report"; \
	  fi; \
	  dashes=`echo "$$dashes" | sed s/./=/g`; \
	  echo "$$dashes"; \
	  echo "$$banner"; \
	  test -z "$$skipped" || echo "$$skipped"; \
	  test -z "$$report" || echo "$$report"; \
	  echo "$$dashes"; \
	  test "$$failed" -eq 0; \
	else :; fi


run-with-tmp-session-bus.conf: run-with-tmp-session-bus.conf.in Makefile
	@sed -e "s|\@abs_top_builddir\@|@abs_top_builddir@|" $< > $@
	
test-basic-connect.sh: run-with-tmp-session-bus.conf

# Dbus service file for testing
service_in_files = org.freedesktop.Telepathy.ConnectionManager.gabble.service.in
service_files = $(service_in_files:.service.in=.service)

BUILT_FILES = $(service_files) run-with-tmp-session-bus.conf

EXTRA_DIST = 

CLEANFILES = $(BUILT_FILES)


