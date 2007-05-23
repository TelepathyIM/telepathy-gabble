SUBDIRS = tools

tools_dir = $(srcdir)/tools

include _gen/spec-gen.am

EXTRA_DIST = $(SPEC_INTERFACE_XMLS) all.xml

SPEC_GENERATED_SOURCES = \
    $(SPEC_GENERATED_CS) \
    $(SPEC_GENERATED_HS) \
    $(SPEC_GENERATED_LISTS) \
    $(SPEC_GLUE_HS)

noinst_LTLIBRARIES = libgabble-extensions.la

libgabble_extensions_la_SOURCES = \
    extensions.h

nodist_libgabble_extensions_la_SOURCES = \
    _gen/signals-marshal.c \
    _gen/signals-marshal.h \
    _gen/signals-marshal.list \
    _gen/enums.h \
    _gen/interfaces.h \
    _gen/svc.h \
    $(SPEC_GENERATED_SOURCES)

BUILT_SOURCES = $(nodist_libgabble_extensions_la_SOURCES) extensions.html

CLEANFILES = $(BUILT_SOURCES) _gen/.exists

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @TP_GLIB_CFLAGS@ @HANDLE_LEAK_DEBUG_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @TP_GLIB_LIBS@

# Generated stuff

DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
XSLTPROCFLAGS = --nonet --novalid

_gen/.exists:
	$(INSTALL) -d _gen
	touch $@

extensions.html: all.xml $(SPEC_INTERFACE_XMLS) $(tools_dir)/doc-generator.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude \
		$(tools_dir)/doc-generator.xsl \
		all.xml > $@

_gen/%.xml: %.xml $(tools_dir)/spec-to-introspect.xsl _gen/.exists
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(tools_dir)/spec-to-introspect.xsl $< \
		| $(DROP_NAMESPACE) > $@

_gen/async-%.xml: _gen/%.xml $(tools_dir)/make-all-async.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) $(tools_dir)/make-all-async.xsl $< > $@

_gen/svc-%-glue.h: _gen/async-%.xml
	$(DBUS_BINDING_TOOL) --mode=glib-server --output=$@ \
		--prefix=gabble_svc_`echo $* | tr A-Z a-z` $<

_gen/svc-%.c _gen/svc-%.h _gen/svc-%-signals-marshal.list: %.xml $(tools_dir)/genginterface.py _gen/.exists
	$(PYTHON) $(tools_dir)/genginterface.py \
		--filename=_gen/svc-$* --signal-marshal-prefix=_gabble_ext \
		--include='<telepathy-glib/dbus.h>' \
		--include='"signals-marshal.h"' \
		--not-implemented-func='tp_dbus_g_method_return_not_implemented' \
		$< GabbleSvc`echo $* | tr -d _`

_gen/signals-marshal.list: $(SPEC_GENERATED_LISTS)
	sort $^ /dev/null | uniq > $@

_gen/signals-marshal.h: _gen/signals-marshal.list
	$(GLIB_GENMARSHAL) --header --prefix=_gabble_ext_marshal $< > $@

_gen/signals-marshal.c: _gen/signals-marshal.list
	$(GLIB_GENMARSHAL) --body --prefix=_gabble_ext_marshal $< > $@

_gen/svc.h: Makefile.in _gen/.exists
	for i in $(SPEC_INTERFACES); \
	do \
		echo "#include \"extensions/_gen/svc-$$i.h\""; \
	done > $@

_gen/enums.h: all.xml tools/c-constants-generator.xsl _gen/.exists
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		$(srcdir)/tools/c-constants-generator.xsl \
		$(srcdir)/all.xml > $@

_gen/interfaces.h: all.xml tools/c-interfaces-generator.xsl _gen/.exists
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		$(srcdir)/tools/c-interfaces-generator.xsl \
		$(srcdir)/all.xml > $@
