ACLOCAL_AMFLAGS = -I m4

SUBDIRS = lib src data m4 tests

EXTRA_DIST = gtk-doc.make

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc --disable-debug

dist-hook:
	chmod a+w ${distdir}/ChangeLog
	if test -d _darcs; then darcs changes >${distdir}/ChangeLog; fi
