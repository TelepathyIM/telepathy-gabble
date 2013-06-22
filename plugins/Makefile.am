installable_plugins = \
	libconsole.la \
	libgateways.la

libtest_only_plugins = \
	libtest.la

# libtesting-only plugins
if ENABLE_INSTALLED_TESTS
noinst_LTLIBRARIES = \
	$(NULL)

libtestplugindir = $(gabbletestsdir)/plugins
libtestplugin_LTLIBRARIES = \
	$(libtest_only_plugins) \
	$(NULL)

libtest_la_LDFLAGS = $(AM_LDFLAGS)
else
noinst_LTLIBRARIES = \
	$(libtest_only_plugins) \
	$(NULL)

# because libtest.la is not installed, libtool will want to compile it as static
# despite -shared (a convenience library), unless we also use -rpath
libtest_la_LDFLAGS = $(AM_LDFLAGS) -rpath $(pluginexecdir)
endif

if ENABLE_PLUGINS
pluginexec_LTLIBRARIES = $(installable_plugins)

dist_bin_SCRIPTS = \
	telepathy-gabble-xmpp-console
else
# we still compile the plugin (just to make sure it compiles!) but we don't
# install it
noinst_LTLIBRARIES += $(installable_plugins)

EXTRA_DIST = \
	telepathy-gabble-xmpp-console
endif

AM_LDFLAGS = -avoid-version -shared -no-undefined

ALL_PLUGIN_LIBS = \
	@WOCKY_LIBS@ \
	@GLIB_LIBS@ \
	@TP_GLIB_LIBS@ \
	$(top_builddir)/extensions/libgabble-extensions.la \
	$(top_builddir)/src/libgabble-plugins.la

libtest_la_LIBADD = $(ALL_PLUGIN_LIBS)
libgateways_la_LIBADD = $(ALL_PLUGIN_LIBS)
libconsole_la_LIBADD = $(ALL_PLUGIN_LIBS)

libtest_la_SOURCES = \
    test.c \
    test.h

libgateways_la_SOURCES = \
	gateways.c \
	gateways.h

libconsole_la_SOURCES = \
	console/debug.c \
	console/debug.h \
	console/plugin.c \
	console/plugin.h \
	console/sidecar.c \
	console/sidecar.h \
	$(NULL)

AM_CFLAGS = $(ERROR_CFLAGS) \
    -I $(top_srcdir) -I $(top_builddir) \
    @DBUS_CFLAGS@ @GLIB_CFLAGS@ @WOCKY_CFLAGS@ @TP_GLIB_CFLAGS@ \
    -I $(top_srcdir)/gabble -I $(top_builddir)/gabble \
    -I $(top_srcdir)/plugins
