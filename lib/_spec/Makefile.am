tpgincludedir=$(includedir)/telepathy-1.0/telepathy-glib

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ \
	    @HANDLE_LEAK_DEBUG_CFLAGS@ \
	    -I$(srcdir)/../_spec \
	    -I$(srcdir)/../_spec/telepathy-glib \
	    -I$(srcdir)/../_gen
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@

include $(srcdir)/Makefile.am.gen
