noinst_PROGRAMS = \
    test-handle-set \
    test-heap \
    test-intset \
    test-util \
    test-internal-debug

TESTS = $(noinst_PROGRAMS)

test_handle_set_SOURCES = \
    test-intset.c
test_handle_set_LDADD = \
    ../telepathy-glib/libtelepathy-glib.la

test_heap_SOURCES = \
    test-heap.c
test_heap_LDADD = \
    ../telepathy-glib/libtelepathy-glib.la

test_util_SOURCES = \
    test-util.c
test_util_LDADD = \
    ../telepathy-glib/libtelepathy-glib.la

test_intset_SOURCES = \
    test-intset.c
test_intset_LDADD = \
    ../telepathy-glib/libtelepathy-glib.la

test_internal_debug_SOURCES = \
    test-internal-debug.c
test_internal_debug_LDADD = \
    ../telepathy-glib/libtelepathy-glib.la

whitespace_sources = *.c

check-local::
	@echo "Checking whitespace-related trivia..."
	@if grep -n ' $$' $(whitespace_sources); \
	then \
		echo "^^^ The above files contain unwanted trailing spaces"; \
		exit 1; \
	fi
	@if grep -n '	' $(whitespace_sources); \
	then \
		echo "^^^ The above files contain tabs"; \
		exit 1; \
	fi

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @COVERAGE_CFLAGS@ -I.. -I$(srcdir)/..
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@
