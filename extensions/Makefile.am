
tools_dir = tools

# This is a derived file but must be checked-in to Darcs anyway,
# for bootstrapping.
include _gen/spec-gen.am

SPEC_GENERATED_SOURCES = \
    $(SPEC_GENERATED_CS) \
    $(SPEC_GENERATED_HS) \
    $(SPEC_GENERATED_LISTS) \
    $(SPEC_GLUE_HS)

noinst_LTLIBRARIES = libgabble-extensions.la

nodist_libgabble_extensions_la_SOURCES = \
    _gen/signals-marshal.c \
    _gen/signals-marshal.h \
    _gen/signals-marshal.list \
    $(SPEC_GENERATED_SOURCES)

BUILT_SOURCES = $(nodist_libgabble_extensions_la_SOURCES)

CLEANFILES = $(BUILT_SOURCES)

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @TP_GLIB_CFLAGS@ @HANDLE_LEAK_DEBUG_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @TP_GLIB_LIBS@

# Generated stuff

DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
XSLTPROCFLAGS = --nonet --novalid

_gen/%.xml: %.xml $(tools_dir)/spec-to-introspect.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(tools_dir)/spec-to-introspect.xsl $< \
		| $(DROP_NAMESPACE) > $@

_gen/async-%.xml: _gen/%.xml $(tools_dir)/make-all-async.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) $(tools_dir)/make-all-async.xsl $< > $@

_gen/svc-%-glue.h: _gen/async-%.xml
	$(DBUS_BINDING_TOOL) --mode=glib-server --output=$@ \
		--prefix=gabble_svc_`echo $* | tr A-Z a-z` $<

_gen/svc-%.c _gen/svc-%.h _gen/svc-%-signals-marshal.list: %.xml $(tools_dir)/genginterface.py
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
