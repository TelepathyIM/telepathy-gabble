noinst_PROGRAMS = test-intset test-gabble-presence test-heap test-handles

test_intset_SOURCES = \
    test-intset.c

test_intset_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la

test_gabble_presence_SOURCES = \
    test-gabble-presence.c

test_gabble_presence_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la

test_heap_SOURCES = \
    test-heap.c

test_heap_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la

test_handles_SOURCES = \
    test-handles.c

test_handles_LDADD = \
    $(top_builddir)/src/libgabble-convenience.la

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@ \
    -I $(top_srcdir)/src -I $(top_builddir)/src
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @LOUDMOUTH_LIBS@

TESTS = $(noinst_PROGRAMS)

