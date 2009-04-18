include $(top_srcdir)/tools/shave.mk

EXTRA_DIST = \
    c-constants-gen.py \
    check-coding-style.mk \
    check-c-style.sh \
    check-misc.sh \
    check-whitespace.sh \
    doc-generator.xsl \
    glib-ginterface-gen.py \
    glib-gtypes-generator.py \
    glib-interfaces-gen.py \
    glib-signals-marshal-gen.py \
    identity.xsl \
    lcov.am \
    libglibcodegen.py \
    libtpcodegen.py \
    shave.mk \
    telepathy.am \
    xep.xsl

CLEANFILES = *.pyc *.pyo

all: $(EXTRA_DIST)

libglibcodegen.py: libtpcodegen.py
	test -e $@
	$(QUIET_GEN)touch $@

glib-ginterface-gen.py glib-gtypes-generator.py glib-interfaces-gen.py \
glib-signals-marshal-gen.py c-constants-gen.py: %: libglibcodegen.py
	test -e $@
	$(QUIET_GEN)touch $@

maintainer-update-from-xmpp.org:
	set -e; \
	uri=svn://svn.xmpp.org:7938/xmpp/trunk/extensions/xep.xsl; \
	svn info $$uri; \
	svn cat $$uri > xep.xsl.tmp
	mv xep.xsl.tmp xep.xsl

TELEPATHY_GLIB_SRCDIR = $(top_srcdir)/../telepathy-glib
maintainer-update-from-telepathy-glib:
	set -e && cd $(srcdir) && \
	for x in $(EXTRA_DIST); do \
		if test -f $(TELEPATHY_GLIB_SRCDIR)/tools/$$x; then \
			cp $(TELEPATHY_GLIB_SRCDIR)/tools/$$x $$x; \
		fi; \
	done
