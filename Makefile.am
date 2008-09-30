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

maintainer-upload-release:
	@case @VERSION@ in \
		(*.*.*.*) \
			echo "@VERSION@ is not a release" >&2; \
			exit 2; \
			;; \
	esac
	test -f @PACKAGE@-@VERSION@.tar.gz
	test -f @PACKAGE@-@VERSION@.tar.gz.asc
	gpg --verify @PACKAGE@-@VERSION@.tar.gz.asc
	rsync -vzP @PACKAGE@-@VERSION@.tar.gz telepathy.freedesktop.org:/srv/telepathy.freedesktop.org/www/releases/@PACKAGE@/
