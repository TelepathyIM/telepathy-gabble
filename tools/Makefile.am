EXTRA_DIST = \
    c-constants-generator.xsl \
    c-interfaces-generator.xsl \
    check-coding-style.mk \
    check-c-style.sh \
    check-misc.sh \
    check-whitespace.sh \
    doc-generator.xsl \
    glib-ginterface-gen.py \
    glib-signals-marshal-gen.py \
    libglibcodegen.py \
    ls-interfaces.xsl \
    make-all-async.xsl \
    spec-to-introspect.xsl \
    update-spec-gen-am.sh

TELEPATHY_GLIB_SRCDIR = $(top_srcdir)/../telepathy-glib
maintainer-update-from-telepathy-glib:
	set -e && cd $(srcdir) && \
	for x in $(EXTRA_DIST); do \
		if test -f $(TELEPATHY_GLIB_SRCDIR)/tools/$$x; then \
			cp $(TELEPATHY_GLIB_SRCDIR)/tools/$$x $$x; \
		fi; \
	done
