tools_dir = $(top_srcdir)/tools

EXTRA_DIST = \
    all.xml \
    OLPC_Activity_Properties.xml \
    OLPC_Buddy_Info.xml

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
    _gen/svc.c

BUILT_SOURCES = \
    _gen/all.xml \
    $(nodist_libgabble_extensions_la_SOURCES) \
    extensions.html

CLEANFILES = $(BUILT_SOURCES)

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @TP_GLIB_CFLAGS@ @HANDLE_LEAK_DEBUG_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @TP_GLIB_LIBS@

# Generated stuff

DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
XSLTPROCFLAGS = --nonet --novalid

_gen/all.xml: all.xml $(wildcard *.xml)
	$(mkdir_p) _gen
	$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude $(tools_dir)/identity.xsl \
		$< > $@

extensions.html: _gen/all.xml $(tools_dir)/doc-generator.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(tools_dir)/doc-generator.xsl \
		$(srcdir)/all.xml > $@

_gen/svc.c _gen/svc.h: _gen/all.xml $(tools_dir)/glib-ginterface-gen.py
	$(PYTHON) $(tools_dir)/glib-ginterface-gen.py \
		--filename=_gen/svc --signal-marshal-prefix=_gabble_ext \
		--include='<telepathy-glib/dbus.h>' \
		--include='"_gen/signals-marshal.h"' \
		--not-implemented-func='tp_dbus_g_method_return_not_implemented' \
		$< Gabble_Svc_

_gen/signals-marshal.list: _gen/all.xml $(tools_dir)/glib-signals-marshal-gen.py
	$(PYTHON) $(tools_dir)/glib-signals-marshal-gen.py $< > $@

_gen/signals-marshal.h: _gen/signals-marshal.list
	$(GLIB_GENMARSHAL) --header --prefix=_gabble_ext_marshal $< > $@

_gen/signals-marshal.c: _gen/signals-marshal.list
	$(GLIB_GENMARSHAL) --body --prefix=_gabble_ext_marshal $< > $@

_gen/enums.h: _gen/all.xml $(tools_dir)/c-constants-generator.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix Gabble \
		$(tools_dir)/c-constants-generator.xsl \
		$< > $@

_gen/interfaces.h: _gen/all.xml $(tools_dir)/c-interfaces-generator.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix Gabble \
		$(tools_dir)/c-interfaces-generator.xsl \
		$< > $@
