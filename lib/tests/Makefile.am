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

# this needs to link against the static convenience library so that
# _tp_debug is still visible
test_internal_debug_SOURCES = \
    test-internal-debug.c
test_internal_debug_LDADD = \
    ../telepathy-glib/libtelepathy-glib-internal.la

check_c_sources = *.c
include $(top_srcdir)/check-coding-style.mk

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @COVERAGE_CFLAGS@ -I.. -I$(srcdir)/..
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@
