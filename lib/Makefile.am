tpgincludedir=$(includedir)/telepathy-1.0/telepathy-glib

noinst_LTLIBRARIES = libtelepathy-glib.la

tpginclude_HEADERS = \
    telepathy-glib/tp-intset.h

libtelepathy_glib_la_SOURCES = \
    intset.c

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @HANDLE_LEAK_DEBUG_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@
