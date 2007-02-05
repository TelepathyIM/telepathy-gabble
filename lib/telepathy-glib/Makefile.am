tpgincludedir=$(includedir)/telepathy-1.0/telepathy-glib
genincludedir=$(tpgincludedir)/_gen

# This is a derived file but must be checked-in to Darcs anyway,
# for bootstrapping.
spec-gen.am: Makefile.am ../spec/all.xml $(SPEC_INTERFACE_XMLS) \
	../tools/ls-interfaces.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude \
		$(srcdir)/../tools/ls-interfaces.xsl \
		$(srcdir)/../spec/all.xml > spec-gen.tmp
	@echo Interfaces to be generated are:
	@cat spec-gen.tmp ; echo
	@echo end of interfaces list
	(\
	echo SPEC_INTERFACES = `cat spec-gen.tmp` && \
	echo SPEC_INTERFACE_XMLS = `sed -e 's,[A-Za-z_][A-Za-z_]*,../spec/&.xml,g' spec-gen.tmp` && \
	echo SPEC_GENERATED_CS = `sed -e 's,[A-Za-z_][A-Za-z_]*,_gen/svc-&.c,g' spec-gen.tmp` && \
	echo SPEC_GENERATED_HS = `sed -e 's,[A-Za-z_][A-Za-z_]*,_gen/svc-&.h,g' spec-gen.tmp` && \
	echo SPEC_GLUE_HS = `sed -e 's,[A-Za-z_][A-Za-z_]*,_gen/svc-&-glue.h,g' spec-gen.tmp` && \
	echo SPEC_GENERATED_LISTS = `sed -e 's,[A-Za-z_][A-Za-z_]*,_gen/svc-&-signals-marshal.list,g' spec-gen.tmp` \
	) > spec-gen.am
	rm -f spec-gen.tmp

include spec-gen.am

SPEC_GENERATED_SOURCES = $(SPEC_GENERATED_CS) $(SPEC_GENERATED_HS) \
			 $(SPEC_GENERATED_LISTS) $(SPEC_GLUE_HS)

lib_LTLIBRARIES = libtelepathy-glib.la

# when it becomes stable enough to have a shared library:
#libtelepathy_glib_la_LDFLAGS = \
#    -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
#    -export-symbols-regex '^tp_'
# but for now:
libtelepathy_glib_la_LDFLAGS = -static

tpginclude_HEADERS = \
    base-connection.h \
    base-connection-manager.h \
    channel-iface.h \
    channel-factory-iface.h \
    dbus.h \
    debug.h \
    debug-ansi.h \
    enums.h \
    errors.h \
    group-mixin.h \
    handle.h \
    handle-repo.h \
    handle-repo-static.h \
    handle-repo-dynamic.h \
    heap.h \
    interfaces.h \
    intset.h \
    properties-mixin.h \
    svc-channel.h \
    svc-connection.h \
    svc-connection-manager.h \
    svc-media-interfaces.h \
    svc-properties-interface.h \
    text-mixin.h \
    util.h

nodist_libtelepathy_glib_la_SOURCES = \
    _gen/signals-marshal.c \
    _gen/signals-marshal.h \
    _gen/signals-marshal.list \
    _gen/enumtypes-spec-telepathy-errors.c \
    $(SPEC_GENERATED_SOURCES)

nodist_geninclude_HEADERS = \
    _gen/telepathy-enums.h \
    _gen/telepathy-interfaces.h \
    _gen/telepathy-errors.h \
    _gen/enumtypes-spec-telepathy-errors.h \
    $(SPEC_GENERATED_HS)

BUILT_SOURCES = $(nodist_geninclude_HEADERS) \
		$(nodist_libtelepathy_glib_la_SOURCES)
CLEANFILES = $(BUILT_SOURCES) _gen/.exists
clean-local:
	-rmdir _gen

libtelepathy_glib_la_SOURCES = \
    base-connection.c \
    base-connection-manager.c \
    dbus.c \
    debug.c \
    internal-debug.h \
    errors.c \
    group-mixin.c \
    handle-repo.c \
    handle-repo-dynamic.c \
    handle-repo-static.c \
    handle-set.c \
    heap.c \
    intset.c \
    channel-iface.c \
    channel-factory-iface.c \
    properties-mixin.c \
    signals-marshal.list \
    text-mixin.c \
    util.c

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @HANDLE_LEAK_DEBUG_CFLAGS@ -I.. -I$(srcdir)/..
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@

# Generated stuff

DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
XSLTPROCFLAGS = --nonet --novalid

_gen/.exists:
	$(INSTALL) -d _gen
	touch $@

_gen/telepathy-enums.h: ../spec/all.xml \
	$(SPEC_INTERFACE_XMLS) ../tools/c-constants-generator.xsl _gen/.exists
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/c-constants-generator.xsl \
		$(srcdir)/../spec/all.xml > $@

_gen/telepathy-interfaces.h: ../spec/all.xml \
	$(SPEC_INTERFACE_XMLS) ../tools/c-interfaces-generator.xsl _gen/.exists
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/c-interfaces-generator.xsl \
		$(srcdir)/../spec/all.xml > $@

_gen/telepathy-errors.h: ../spec/errors.xml \
	../tools/c-errors-enum-generator.xsl _gen/.exists
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/c-errors-enum-generator.xsl \
		$< > $@

_gen/%.xml: ../spec/%.xml ../tools/spec-to-introspect.xsl _gen/.exists
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/spec-to-introspect.xsl $< \
		| $(DROP_NAMESPACE) > $@

_gen/async-%.xml: _gen/%.xml ../tools/make-all-async.xsl _gen/.exists
	$(XSLTPROC) $(XSLTPROCFLAGS) $(srcdir)/../tools/make-all-async.xsl $< > $@

_gen/svc-%-glue.h: _gen/async-%.xml _gen/.exists
	$(DBUS_BINDING_TOOL) --mode=glib-server --output=$@ \
		--prefix=tp_svc_`echo $* | tr A-Z a-z` $<

_gen/svc-%.c _gen/svc-%.h _gen/svc-%-signals-marshal.list: ../spec/%.xml ../tools/genginterface.py _gen/.exists
	$(PYTHON) $(srcdir)/../tools/genginterface.py $< \
	TpSvc`echo $* | tr -d _` _gen/svc-$* _tp

_gen/signals-marshal.list: signals-marshal.list $(SPEC_GENERATED_LISTS)
	sort $^ | uniq > $@

_gen/signals-marshal.h: _gen/signals-marshal.list _gen/.exists
	$(GLIB_GENMARSHAL) --header --prefix=_tp_marshal $< > $@

_gen/signals-marshal.c: _gen/signals-marshal.list _gen/.exists
	$(GLIB_GENMARSHAL) --body --prefix=_tp_marshal $< > $@

_gen/enumtypes-spec-%.c: _gen/%.h Makefile.am _gen/.exists
	glib-mkenums \
	--fhead '#include "_gen/$*.h"\n#include "_gen/enumtypes-spec-$*.h"' \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@VALUENAME@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
        $< > $@

_gen/enumtypes-spec-%.h: _gen/%.h Makefile.am _gen/.exists
	glib-mkenums \
	--fhead "#ifndef __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n#define __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define TP_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__ */" \
        $< > $@
