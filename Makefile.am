ACLOCAL_AMFLAGS = -I m4

SUBDIRS = docs tools extensions lib src data m4 tests

DISTCHECK_CONFIGURE_FLAGS = --disable-debug

check-twisted: all
	$(MAKE) -C tests/twisted check-twisted

check-all: check check-twisted

include tools/lcov.am
LCOV_CHECK_ARGS = CHECK_TWISTED_SLEEP=6

include tools/telepathy.am
