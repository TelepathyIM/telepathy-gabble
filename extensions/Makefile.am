tools_dir = $(top_srcdir)/tools

EXTRA_DIST = \
    all.xml \
    Channel_Type_FileTransfer_Future.xml \
    Connection_Future.xml \
    Connection_Interface_Addressing.xml \
    Connection_Interface_Gabble_Decloak.xml \
    Gabble_Plugin_Console.xml \
    Gabble_Plugin_Gateways.xml \
    Gabble_Plugin_Test.xml \
    OLPC_Activity_Properties.xml \
    OLPC_Buddy_Info.xml \
    $(NULL)

noinst_LTLIBRARIES = libgabble-extensions.la

libgabble_extensions_la_SOURCES = \
    extensions.c \
    extensions.h

nodist_libgabble_extensions_la_SOURCES = \
    _gen/enums.h \
    _gen/enums-gtk-doc.h \
    _gen/gtypes.h \
    _gen/gtypes-body.h \
    _gen/gtypes-gtk-doc.h \
    _gen/interfaces.h \
    _gen/interfaces-body.h \
    _gen/interfaces-gtk-doc.h \
    _gen/svc.h \
    _gen/svc.c \
    _gen/svc-gtk-doc.h \
    $(NULL)

BUILT_SOURCES = \
    _gen/all.xml \
    $(nodist_libgabble_extensions_la_SOURCES) \
    extensions.html

CLEANFILES = $(BUILT_SOURCES)

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @TP_GLIB_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @TP_GLIB_LIBS@

# Generated stuff

DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
XSLTPROCFLAGS = --nonet --novalid

_gen/all.xml: all.xml $(wildcard *.xml) Makefile.am
	@$(mkdir_p) _gen
	$(AM_V_GEN)$(PYTHON) $(tools_dir)/xincludator.py \
		$< > $@.tmp && mv $@.tmp $@

extensions.html: _gen/all.xml $(tools_dir)/doc-generator.xsl Makefile.am
	$(AM_V_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) \
		--param "allow-undefined-interfaces" "true()" \
		$(tools_dir)/doc-generator.xsl \
		$< > $@

_gen/svc.h: _gen/svc.c
	@: # do nothing, output as a side-effect
_gen/svc-gtk-doc.h: _gen/svc.c
	@: # do nothing, output as a side-effect
_gen/svc.c: _gen/all.xml $(tools_dir)/glib-ginterface-gen.py Makefile.am
	$(AM_V_GEN)$(PYTHON) $(tools_dir)/glib-ginterface-gen.py \
		--filename=_gen/svc \
		--include='<telepathy-glib/telepathy-glib.h>' \
		--allow-unstable \
		--not-implemented-func='tp_dbus_g_method_return_not_implemented' \
		$< Gabble_Svc_

_gen/enums-gtk-doc.h: _gen/enums.h
	@: # do nothing, output as a side-effect
_gen/enums.h: _gen/all.xml $(tools_dir)/c-constants-gen.py Makefile.am
	$(AM_V_GEN)$(PYTHON) $(tools_dir)/c-constants-gen.py Gabble $< _gen/enums

_gen/interfaces-body.h: _gen/interfaces.h
	@: # do nothing, output as a side-effect
_gen/interfaces-gtk-doc.h: _gen/interfaces.h
	@: # do nothing, output as a side-effect
_gen/interfaces.h: _gen/all.xml $(tools_dir)/glib-interfaces-gen.py Makefile.am
	$(AM_V_GEN)$(PYTHON) $(tools_dir)/glib-interfaces-gen.py \
		Gabble _gen/interfaces-body.h _gen/interfaces.h $<

_gen/gtypes.h: _gen/gtypes-body.h
	@: # do nothing, output as a side-effect
_gen/gtypes-gtk-doc.h: _gen/gtypes-body.h
	@: # do nothing, output as a side-effect

_gen/gtypes-body.h: _gen/all.xml $(tools_dir)/glib-gtypes-generator.py Makefile.am
	$(AM_V_GEN)$(PYTHON) $(tools_dir)/glib-gtypes-generator.py \
		$< _gen/gtypes Gabble

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer -:PROJECT telepathy-gabble -:STATIC gabble-extensions -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgabble_extensions_la_SOURCES) \
	  $(nodist_libgabble_extensions_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(CFLAGS) $(DEFAULT_INCLUDES) $(INCLUDES) \
	  $(AM_CFLAGS) \
	 -:CPPFLAGS $(CPPFLAGS) $(AM_CPPFLAGS) \
	 -:LDFLAGS $(libgabble_extensions_la_LIBADD) \
	> $@
