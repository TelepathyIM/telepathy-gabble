# temporary hack to not install the headers until telepathy-glib is
# releasable separately - using noinst_HEADERS seemed to
# cause build problems.
install:
	@echo Not installing headers until telepathy-glib is stable

SUBDIRS = _spec

.PHONY: maintainer-update-from-spec
maintainer-update-from-spec:
	@test z. = z$(srcdir) || \
		{ echo "maintainer-update-from-spec doesn't work in an"\
		"out of tree build"; \
		exit 1; }
	$(MAKE) -C _spec -f ../update-from-spec.mk

tpgincludedir=$(includedir)/telepathy-1.0/telepathy-glib
specincludedir=$(tpgincludedir)/_spec
genincludedir=$(tpgincludedir)/_gen

noinst_LTLIBRARIES = libtelepathy-glib.la

libtelepathy_glib_la_LIBADD = _spec/libtelepathy-glib-_spec.la

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
    telepathy-glib/text-mixin.h \
    telepathy-glib/util.h

nodist_libtelepathy_glib_la_SOURCES = \
    _gen/signals-marshal.c \
    _gen/signals-marshal.h \
    _gen/enumtypes-spec-telepathy-errors.c

nodist_geninclude_HEADERS = \
    telepathy-glib/_gen/telepathy-enums.h \
    telepathy-glib/_gen/telepathy-interfaces.h \
    telepathy-glib/_gen/telepathy-errors.h \
    telepathy-glib/_gen/enumtypes-spec-telepathy-errors.h

BUILT_SOURCES = $(nodist_geninclude_HEADERS) \
		$(nodist_libtelepathy_glib_la_SOURCES)
CLEANFILES = $(BUILT_SOURCES)

libtelepathy_glib_la_SOURCES = \
    base-connection.c \
    base-connection-manager.c \
    dbus.c \
    debug.c \
    debug.h \
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

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @HANDLE_LEAK_DEBUG_CFLAGS@ -I$(srcdir)/_spec
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@

include spec.am

# Generated stuff

XSLTPROCFLAGS = --nonet

telepathy-glib/_gen/telepathy-enums.h: spec/all.xml \
	$(SPEC_INTERFACE_XMLS) tools/c-constants-generator.xsl
	$(INSTALL) -d telepathy-glib/_gen
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) tools/c-constants-generator.xsl \
		spec/all.xml > $@

telepathy-glib/_gen/telepathy-interfaces.h: spec/all.xml \
	$(SPEC_INTERFACE_XMLS) tools/c-interfaces-generator.xsl
	$(INSTALL) -d telepathy-glib/_gen
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) tools/c-interfaces-generator.xsl \
		spec/all.xml > $@

telepathy-glib/_gen/telepathy-errors.h: spec/errors.xml \
	tools/c-errors-enum-generator.xsl
	$(INSTALL) -d telepathy-glib/_gen
	$(XSLTPROC) $(XSLTPROCFLAGS) tools/c-errors-enum-generator.xsl \
		$< > $@

_gen/signals-marshal.h: signals-marshal.list
	$(INSTALL) -d _gen
	glib-genmarshal --header --prefix=_tp_marshal $< > $@

_gen/signals-marshal.c: signals-marshal.list
	$(INSTALL) -d _gen
	glib-genmarshal --body --prefix=_tp_marshal $< > $@

_gen/enumtypes-spec-%.c: telepathy-glib/_gen/%.h Makefile.in
	$(INSTALL) -d _gen
	glib-mkenums \
	--fhead "#include <telepathy-glib/_spec/$*.h>\n#include <telepathy-glib/_gen/enumtypes-spec-$*.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@VALUENAME@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
        $< > $@

telepathy-glib/_gen/enumtypes-spec-%.h: telepathy-glib/_gen/%.h Makefile.in
	$(INSTALL) -d telepathy-glib/_gen
	glib-mkenums \
	--fhead "#ifndef __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n#define __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define TP_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__ */" \
        $< > $@
