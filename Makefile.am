ACLOCAL_AMFLAGS = -I m4

SUBDIRS = docs tools extensions lib src data m4 tests

DISTCHECK_CONFIGURE_FLAGS = --disable-debug

CLEANFILES = FIXME.out

check-twisted: all
	$(MAKE) -C tests/twisted check-twisted

check-all: check check-twisted

check-local::
	egrep -A 5 '[F]IXME|[T]ODO|[X]XX' $(srcdir)/src/*.[ch] \
		> FIXME.out || true

include tools/lcov.am
LCOV_CHECK_ARGS = CHECK_TWISTED_SLEEP=6

CHANGELOG_RANGE = telepathy-gabble-0.6.0..
include tools/telepathy.am
