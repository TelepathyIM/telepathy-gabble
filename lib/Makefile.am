SUBDIRS = tools spec telepathy-glib tests docs
EXTRA_DIST = telepathy-glib.pc.in

pkgconfigdir = ${libdir}/pkgconfig
pkgconfig_DATA = telepathy-glib.pc

if ENABLE_UNSTABLE
pkgconfig_DATA += telepathy-glib-unstable.pc
endif

check-local::
	egrep -A 5 '[F]IXME|[T]ODO|[X]XX' $(srcdir)/telepathy-glib/*.[ch] \
		> FIXME.out || true

CLEANFILES = FIXME.out
