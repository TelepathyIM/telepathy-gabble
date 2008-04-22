tools_dir = $(top_srcdir)/tools

man_MANS = telepathy-gabble.8
html_DATA = \
    olpc.html \
    muc-bytestream.html \
    tubes.html

EXTRA_DIST = \
    $(man_MANS:.8=.8.in) \
    $(html_DATA:.html=.xml)

CLEANFILES = \
    $(man_MANS) \
    $(html_DATA)

%.8: %.8.in Makefile
	sed -e 's,[@]libexecdir[@],@libexecdir@,' < $< > $@

$(html_DATA): %.html: %.xml $(tools_dir)/xep.xsl
	$(XSLTPROC) $(tools_dir)/xep.xsl $< > $@

proto-xep-upload: $(html_DATA)
	rsync -P $(html_DATA) projects.collabora.co.uk:public_html/
.PHONY: proto-xep-upload
