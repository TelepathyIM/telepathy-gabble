include $(top_srcdir)/tools/shave.mk

tools_dir = $(top_srcdir)/tools

EXTRA_DIST = \
    all.xml \
    Channel_Bundle.xml \
    Channel_Future.xml \
    Connection_Interface_Contact_Capabilities.xml \
    Channel_Type_Streamed_Media_Future.xml \
    Debug.xml \
    OLPC_Activity_Properties.xml \
    OLPC_Buddy_Info.xml \
    OLPC_Gadget.xml \
    OLPC_Channel_Interface_View.xml \
    OLPC_Channel_Type_BuddyView.xml \
    OLPC_Channel_Type_ActivityView.xml

noinst_LTLIBRARIES = libgabble-extensions.la

libgabble_extensions_la_SOURCES = \
    extensions.c \
    extensions.h

nodist_libgabble_extensions_la_SOURCES = \
    _gen/signals-marshal.c \
    _gen/signals-marshal.h \
    _gen/signals-marshal.list \
    _gen/enums.h \
    _gen/gtypes.h \
    _gen/gtypes-body.h \
    _gen/interfaces.h \
    _gen/interfaces-body.h \
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

_gen/all.xml: all.xml $(wildcard *.xml) Makefile.am
	$(mkdir_p) _gen
	$(QUIET_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude $(tools_dir)/identity.xsl \
		$< > $@

extensions.html: _gen/all.xml $(tools_dir)/doc-generator.xsl Makefile.am
	$(QUIET_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) \
		--param "allow-undefined-interfaces" "true()" \
		$(tools_dir)/doc-generator.xsl \
		$< > $@

_gen/svc.c _gen/svc.h: _gen/all.xml $(tools_dir)/glib-ginterface-gen.py \
	Makefile.am
	$(QUIET_GEN)$(PYTHON) $(tools_dir)/glib-ginterface-gen.py \
		--filename=_gen/svc --signal-marshal-prefix=_gabble_ext \
		--include='<telepathy-glib/dbus.h>' \
		--include='"_gen/signals-marshal.h"' \
		--allow-unstable \
		--not-implemented-func='tp_dbus_g_method_return_not_implemented' \
		$< Gabble_Svc_

_gen/signals-marshal.list: _gen/all.xml \
	$(tools_dir)/glib-signals-marshal-gen.py \
	Makefile.am
	$(QUIET_GEN)$(PYTHON) $(tools_dir)/glib-signals-marshal-gen.py $< > $@

_gen/signals-marshal.h: _gen/signals-marshal.list Makefile.am
	$(QUIET_GEN)$(GLIB_GENMARSHAL) --header --prefix=_gabble_ext_marshal $< > $@

_gen/signals-marshal.c: _gen/signals-marshal.list Makefile.am
	$(QUIET_GEN){ echo '#include "_gen/signals-marshal.h"' && \
	$(GLIB_GENMARSHAL) --body --prefix=_gabble_ext_marshal $< ; } > $@

_gen/enums.h: _gen/all.xml $(tools_dir)/c-constants-gen.py \
	Makefile.am
	$(QUIET_GEN)$(PYTHON) $(tools_dir)/c-constants-gen.py Gabble $< > $@

_gen/interfaces.h _gen/interfaces-body.h: _gen/all.xml \
	$(tools_dir)/glib-interfaces-gen.py \
	Makefile.am
	$(QUIET_GEN)$(PYTHON) $(tools_dir)/glib-interfaces-gen.py \
		Gabble _gen/interfaces-body.h _gen/interfaces.h $<

_gen/gtypes.h _gen/gtypes-body.h: _gen/all.xml \
	$(tools_dir)/glib-gtypes-generator.py Makefile.am
	$(QUIET_GEN)$(PYTHON) $(tools_dir)/glib-gtypes-generator.py \
		$< _gen/gtypes Gabble
