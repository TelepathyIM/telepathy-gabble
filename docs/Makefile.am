man_MANS = telepathy-gabble.8

EXTRA_DIST = $(man_MANS:.8=.8.in)
CLEANFILES = $(man_MANS)

%.8: %.8.in Makefile
	sed -e 's,[@]libexecdir[@],@libexecdir@,' < $< > $@

# ---- Proto-XEPs (not automatically generated, see README) ----

PROTO_XEP_XML = \
    muc-bytestream.xml \
    tubes.xml
PROTO_XEP_HTML = $(PROTO_XEP_XML:.xml=.html)

proto-xep-html: $(PROTO_XEP_HTML)
.PHONY: proto-xep-html

$(PROTO_XEP_HTML): %.html: %.xml xep.xsl
	$(XSLTPROC) xep.xsl $< > $@
.PHONY: proto-xep-html

proto-xep-upload: proto-xep-html
	rsync -P $(PROTO_XEP_HTML) projects.collabora.co.uk:public_html/
.PHONY: proto-xep-upload

xep.xsl:
	@echo "Please get xep.xsl from xmpp.org svn - see README for details"
	@exit 1

EXTRA_DIST += $(PROTO_XEP_XML)
