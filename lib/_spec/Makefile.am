tpgincludedir=$(includedir)/telepathy-1.0/telepathy-glib

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ \
	    @HANDLE_LEAK_DEBUG_CFLAGS@ \
	    -I$(srcdir) \
	    -I$(srcdir)/telepathy-glib \
	    -I$(srcdir)/.. \
	    -I$(srcdir)/../telepathy-glib \
	    -I../_gen
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@

include $(srcdir)/Makefile.am.gen
