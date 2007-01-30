# temporary hack to not install the headers until telepathy-glib is
# releasable separately - using noinst_HEADERS seemed to
# cause build problems.
install:
	@echo Not installing headers until telepathy-glib is stable

tpgincludedir=$(includedir)/telepathy-1.0/telepathy-glib
genincludedir=$(tpgincludedir)/_gen

# This is a derived file but must be checked-in to Darcs anyway.
spec-gen.am: Makefile.am spec/all.xml $(SPEC_INTERFACE_XMLS) \
	tools/ls-interfaces.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude \
		$(srcdir)/tools/ls-interfaces.xsl \
		$(srcdir)/spec/all.xml > spec-gen.tmp
	@echo Interfaces to be generated are:
	@cat spec-gen.tmp ; echo
	@echo end of interfaces list
	(\
	echo SPEC_INTERFACES = `cat spec-gen.tmp` && \
	echo SPEC_INTERFACE_XMLS = `sed -e 's,[A-Za-z_][A-Za-z_]*,spec/&.xml,g' spec-gen.tmp` && \
	echo SPEC_GENERATED_CS = `sed -e 's,[A-Za-z_][A-Za-z_]*,_gen/svc-&.c,g' spec-gen.tmp` && \
	echo SPEC_GENERATED_HS = `sed -e 's,[A-Za-z_][A-Za-z_]*,_gen/svc-&.h,g' spec-gen.tmp` && \
	echo SPEC_GLUE_HS = `sed -e 's,[A-Za-z_][A-Za-z_]*,_gen/svc-&-glue.h,g' spec-gen.tmp` && \
	echo SPEC_COPIED_HS = `sed -e 's,[A-Za-z_][A-Za-z_]*,telepathy-glib/_gen/svc-&.h,g' spec-gen.tmp` && \
	echo SPEC_GENERATED_LISTS = `sed -e 's,[A-Za-z_][A-Za-z_]*,_gen/svc-&-signals-marshal.list,g' spec-gen.tmp` \
	) > spec-gen.am
	rm -f spec-gen.tmp

include spec-gen.am

SPEC_GENERATED_SOURCES = $(SPEC_GENERATED_CS) $(SPEC_GENERATED_HS) \
			 $(SPEC_GENERATED_LISTS) $(SPEC_GLUE_HS) \
			 $(SPEC_COPIED_HS)

noinst_LTLIBRARIES = libtelepathy-glib.la

TESTS = check_self_contained

check_PROGRAMS = check_self_contained
check_self_contained_SOURCES = _gen/main.c
check_self_contained_LDADD = $(noinst_LTLIBRARIES)

_gen/main.c: Makefile.in
	echo 'int main (void) { return 0; }' > $@

tpginclude_HEADERS = \
    telepathy-glib/base-connection.h \
    telepathy-glib/base-connection-manager.h \
    telepathy-glib/channel-iface.h \
    telepathy-glib/channel-factory-iface.h \
    telepathy-glib/dbus.h \
    telepathy-glib/debug.h \
    telepathy-glib/debug-ansi.h \
    telepathy-glib/enums.h \
    telepathy-glib/errors.h \
    telepathy-glib/group-mixin.h \
    telepathy-glib/handle.h \
    telepathy-glib/handle-repo.h \
    telepathy-glib/handle-repo-static.h \
    telepathy-glib/handle-repo-dynamic.h \
    telepathy-glib/heap.h \
    telepathy-glib/interfaces.h \
    telepathy-glib/intset.h \
    telepathy-glib/properties-mixin.h \
    telepathy-glib/svc-channel.h \
    telepathy-glib/svc-connection.h \
    telepathy-glib/svc-connection-manager.h \
    telepathy-glib/svc-media-interfaces.h \
    telepathy-glib/svc-properties-interface.h \
    telepathy-glib/text-mixin.h \
    telepathy-glib/util.h

nodist_libtelepathy_glib_la_SOURCES = \
    _gen/signals-marshal.c \
    _gen/signals-marshal.h \
    _gen/signals-marshal.list \
    _gen/enumtypes-spec-telepathy-errors.c \
    $(SPEC_GENERATED_SOURCES)

nodist_geninclude_HEADERS = \
    telepathy-glib/_gen/telepathy-enums.h \
    telepathy-glib/_gen/telepathy-interfaces.h \
    telepathy-glib/_gen/telepathy-errors.h \
    telepathy-glib/_gen/enumtypes-spec-telepathy-errors.h \
    $(SPEC_COPIED_HS)

BUILT_SOURCES = $(nodist_geninclude_HEADERS) \
		$(nodist_libtelepathy_glib_la_SOURCES)
CLEANFILES = $(BUILT_SOURCES) _gen/.exists telepathy-glib/_gen/.exists
clean-local:
	-rmdir _gen telepathy-glib/_gen

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

EXTRA_DIST = \
    spec/all.xml \
    spec/errors.xml \
    $(SPEC_INTERFACE_XMLS) \
    tools/c-constants-generator.xsl \
    tools/c-errors-enum-generator.xsl \
    tools/c-interfaces-generator.xsl \
    tools/genginterface.py \
    tools/ls-interfaces.xsl \
    tools/make-all-async.xsl \
    tools/spec-to-introspect.xsl

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @HANDLE_LEAK_DEBUG_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@

# Generated stuff

DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
XSLTPROCFLAGS = --nonet --novalid

_gen/.exists:
	$(INSTALL) -d _gen
	touch $@
telepathy-glib/_gen/.exists:
	$(INSTALL) -d telepathy-glib/_gen
	touch $@

telepathy-glib/_gen/telepathy-enums.h: spec/all.xml \
	$(SPEC_INTERFACE_XMLS) tools/c-constants-generator.xsl \
	telepathy-glib/_gen/.exists
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		$(srcdir)/tools/c-constants-generator.xsl \
		$(srcdir)/spec/all.xml > $@

telepathy-glib/_gen/telepathy-interfaces.h: spec/all.xml \
	$(SPEC_INTERFACE_XMLS) tools/c-interfaces-generator.xsl \
	telepathy-glib/_gen/.exists
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		$(srcdir)/tools/c-interfaces-generator.xsl \
		$(srcdir)/spec/all.xml > $@

telepathy-glib/_gen/telepathy-errors.h: spec/errors.xml \
	tools/c-errors-enum-generator.xsl telepathy-glib/_gen/.exists
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(srcdir)/tools/c-errors-enum-generator.xsl \
		$< > $@

_gen/%.xml: spec/%.xml tools/spec-to-introspect.xsl telepathy-glib/_gen/.exists
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(srcdir)/tools/spec-to-introspect.xsl $< \
		| $(DROP_NAMESPACE) > $@

_gen/async-%.xml: _gen/%.xml tools/make-all-async.xsl _gen/.exists
	$(XSLTPROC) $(XSLTPROCFLAGS) $(srcdir)/tools/make-all-async.xsl $< > $@

_gen/svc-%-glue.h: _gen/async-%.xml _gen/.exists
	$(DBUS_BINDING_TOOL) --mode=glib-server --output=$@ \
		--prefix=tp_svc_`echo $* | tr A-Z a-z` $<

_gen/svc-%.c _gen/svc-%.h: spec/%.xml tools/genginterface.py _gen/.exists
	$(PYTHON) $(srcdir)/tools/genginterface.py $< \
	TpSvc`echo $* | tr -d _` _gen/svc-$* _tp

telepathy-glib/_gen/svc-%.h: _gen/svc-%.h
	cp $< $@

_gen/signals-marshal.list: signals-marshal.list $(SPEC_GENERATED_LISTS)
	sort $^ | uniq > $@

_gen/signals-marshal.h: _gen/signals-marshal.list _gen/.exists
	$(GLIB_GENMARSHAL) --header --prefix=_tp_marshal $< > $@

_gen/signals-marshal.c: _gen/signals-marshal.list _gen/.exists
	$(GLIB_GENMARSHAL) --body --prefix=_tp_marshal $< > $@

_gen/enumtypes-spec-%.c: telepathy-glib/_gen/%.h Makefile.in _gen/.exists
	glib-mkenums \
	--fhead "#include <telepathy-glib/_gen/$*.h>\n#include <telepathy-glib/_gen/enumtypes-spec-$*.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@VALUENAME@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
        $< > $@

telepathy-glib/_gen/enumtypes-spec-%.h: telepathy-glib/_gen/%.h Makefile.in \
	telepathy-glib/_gen/.exists
	glib-mkenums \
	--fhead "#ifndef __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n#define __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define TP_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__ */" \
        $< > $@
