tools_dir = $(top_srcdir)/tools

# htmldir is not defined by autoconf < 2.59c
htmldir = $(if $(filter-out @%@,@htmldir@),@htmldir@,$(datadir)/doc/$(PACKAGE))

man_MANS = telepathy-gabble.8
html_DATA = \
    olpc.html \
    muc-bytestream.html \
    si-multiple.html \
    tubes.html

EXTRA_DIST = \
    $(man_MANS:.8=.8.in) \
    $(html_DATA:.html=.xml) \
    xep.dtd \
    xep.ent \
    xep.xsd \
    xep.xsl

CLEANFILES = \
    $(man_MANS) \
    $(html_DATA)

%.8: %.8.in Makefile
	$(AM_V_GEN)sed -e 's,[@]libexecdir[@],@libexecdir@,' < $< > $@

$(html_DATA): %.html: %.xml xep.xsl xep.dtd xep.ent
	$(AM_V_GEN)$(XSLTPROC) $(srcdir)/xep.xsl $< > $@

proto-xep-upload: $(html_DATA)
	rsync -P $(html_DATA) people.collabora.co.uk:public_html/
.PHONY: proto-xep-upload

maintainer-update-from-xmpp.org:
	set -e; \
	for x in xep.dtd xep.ent xep.xsd xep.xsl; do \
		uri=svn://svn.xmpp.org:7938/xmpp/trunk/extensions/$$x; \
		svn info $$uri; \
		svn cat $$uri > $$x.tmp; \
		mv $$x.tmp $$x; \
	done
