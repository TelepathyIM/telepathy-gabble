ACLOCAL_AMFLAGS = -I m4

SUBDIRS = docs tools extensions src data m4 tests

DISTCHECK_CONFIGURE_FLAGS = --disable-debug

dist-hook:
	chmod u+w ${distdir}/ChangeLog
	if test -d _darcs; then darcs changes >${distdir}/ChangeLog; fi

check-twisted: all
	$(MAKE) -C tests check-twisted

check-all: check check-twisted
