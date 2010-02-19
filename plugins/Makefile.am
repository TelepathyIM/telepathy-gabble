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

test_la_SOURCES = \
    test.c \
    test.h

test_la_LDFLAGS = -module -avoid-version -rpath $(plugindir) -lm

gateways_la_SOURCES = \
	gateways.c \
	gateways.h

gateways_la_LDFLAGS = -module -avoid-version

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @WOCKY_CFLAGS@ \
    @TP_GLIB_CFLAGS@ \
    -I $(top_gabbledir)/gabble -I $(top_builddir)/gabble \
    -I $(top_srcdir) -I $(top_builddir) \
    -I $(top_srcdir)/plugins
