noinst_PROGRAMS = \
    test-handle-set \
    test-heap \
    test-intset

TESTS = $(noinst_PROGRAMS)

test_handle_set_SOURCES = \
    test-intset.c
test_handle_set_LDADD = \
    ../telepathy-glib/libtelepathy-glib.la

test_heap_SOURCES = \
    test-heap.c
test_heap_LDADD = \
    ../telepathy-glib/libtelepathy-glib.la

test_intset_SOURCES = \
    test-intset.c
test_intset_LDADD = \
    ../telepathy-glib/libtelepathy-glib.la

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ -I.. -I$(srcdir)/..
