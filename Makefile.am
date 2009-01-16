ACLOCAL_AMFLAGS = -I m4

SUBDIRS = docs tools extensions lib src data m4 tests

DISTCHECK_CONFIGURE_FLAGS = --disable-debug

check-twisted: all
	$(MAKE) -C tests/twisted check-twisted

check-all: check check-twisted

include tools/lcov.am
include tools/telepathy.am
