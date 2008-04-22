tools_dir = $(top_srcdir)/tools

man_MANS = telepathy-gabble.8
doc_DATA = \
    olpc.html \
    muc-bytestream.html \
    tubes.html

EXTRA_DIST = \
    $(man_MANS:.8=.8.in) \
    $(doc_DATA:.html=.xml)

CLEANFILES = \
    $(man_MANS) \
    $(doc_DATA)

%.8: %.8.in Makefile
	sed -e 's,[@]libexecdir[@],@libexecdir@,' < $< > $@

$(doc_DATA): %.html: %.xml $(tools_dir)/xep.xsl
	$(XSLTPROC) $(tools_dir)/xep.xsl $< > $@

proto-xep-upload: $(doc_DATA)
	rsync -P $(doc_DATA) projects.collabora.co.uk:public_html/
.PHONY: proto-xep-upload
