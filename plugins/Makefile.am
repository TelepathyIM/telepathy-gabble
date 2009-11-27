plugindir = $(libdir)/telepathy/gabble-0

noinst_LTLIBRARIES = \
	test.la

test_la_SOURCES = \
    test.c \
    test.h

test_la_LDFLAGS = -module -avoid-version -rpath $(plugindir) -lm

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @WOCKY_CFLAGS@ \
    @TP_GLIB_CFLAGS@ \
    -I $(top_srcdir)/src -I $(top_builddir)/src \
    -I $(top_srcdir) -I $(top_builddir) \
    -I $(top_srcdir)/plugins
