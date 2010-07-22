plugindir = $(libdir)/telepathy/gabble-0

installable_plugins = \
	gateways.la

# testing-only plugins
noinst_LTLIBRARIES = \
	test.la

if ENABLE_PLUGINS
plugin_LTLIBRARIES = $(installable_plugins)
else
# we still compile the plugin (just to make sure it compiles!) but we don't
# install it
noinst_LTLIBRARIES += $(installable_plugins)
endif

AM_LDFLAGS = -module -avoid-version -shared

test_la_SOURCES = \
    test.c \
    test.h

# because test.la is not installed, libtool will want to compile it as static
# despite -shared (a convenience library), unless we also use -rpath
test_la_LDFLAGS = $(AM_LDFLAGS) -rpath $(plugindir)

gateways_la_SOURCES = \
	gateways.c \
	gateways.h

AM_CFLAGS = $(ERROR_CFLAGS) \
    -I $(top_srcdir) -I $(top_builddir) \
    @DBUS_CFLAGS@ @GLIB_CFLAGS@ @WOCKY_CFLAGS@ @TP_GLIB_CFLAGS@ \
    -I $(top_gabbledir)/gabble -I $(top_builddir)/gabble \
    -I $(top_srcdir)/plugins
