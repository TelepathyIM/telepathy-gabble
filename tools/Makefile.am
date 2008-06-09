EXTRA_DIST = \
    c-constants-generator.xsl \
    c-interfaces-generator.xsl \
    check-coding-style.mk \
    check-c-style.sh \
    check-misc.sh \
    check-whitespace.sh \
    doc-generator.xsl \
    glib-ginterface-gen.py \
    glib-gtypes-generator.py \
    glib-interfaces-generator.xsl \
    glib-interfaces-body-generator.xsl \
    glib-signals-marshal-gen.py \
    identity.xsl \
    libglibcodegen.py \
    libtpcodegen.py \
    xep.xsl

libglibcodegen.py: libtpcodegen.py
	test -e $@
	touch $@

glib-interfaces-generator.xsl glib-interfaces-body-generator.xsl: %: c-interfaces-generator.xsl
	test -e $@
	touch $@

glib-ginterface-gen.py glib-gtypes-generator.py glib-signals-marshal-gen.py: \
		%: libglibcodegen.py
	test -e $@
	touch $@

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
