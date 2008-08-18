ACLOCAL_AMFLAGS = -I m4

SUBDIRS = docs tools extensions src data m4 tests

DISTCHECK_CONFIGURE_FLAGS = --disable-debug

dist-hook:
	chmod u+w ${distdir}/ChangeLog
	if test -d ${top_srcdir}/.git; then \
		git log --stat > ${distdir}/ChangeLog || \
		git log > ${distdir}/ChangeLog; \
	fi

check-twisted: all
	$(MAKE) -C tests/twisted check-twisted

check-all: check check-twisted
