plugindir = $(libdir)/telepathy/gabble-0

noinst_LTLIBRARIES = \
	test.la

test_la_SOURCES = \
    test.c \
    test.h

test_la_LDFLAGS = -module -avoid-version -rpath $(plugindir) -lm

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @WOCKY_CFLAGS@ \
    @TP_GLIB_CFLAGS@ \
    -I $(top_gabbledir)/gabble -I $(top_builddir)/gabble \
    -I $(top_srcdir) -I $(top_builddir) \
    -I $(top_srcdir)/plugins
