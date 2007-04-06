ACLOCAL_AMFLAGS = -I m4

SUBDIRS = lib src data m4 tests

EXTRA_DIST = \
    check-coding-style.mk \
    check-c-style.sh \
    check-whitespace.sh \
    gtk-doc.make

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc --disable-debug

dist-hook:
	chmod u+w ${distdir}/ChangeLog
	if test -d _darcs; then darcs changes >${distdir}/ChangeLog; fi
