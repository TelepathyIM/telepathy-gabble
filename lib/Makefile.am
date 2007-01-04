# temporary hack to not install the headers until telepathy-glib is
# releasable separately - using noinst_HEADERS seemed to
# cause build problems.
install:
	@echo Not installing headers until telepathy-glib is stable

tpgincludedir=$(includedir)/telepathy-1.0/telepathy-glib
specincludedir=$(tpgincludedir)/_spec
genincludedir=$(tpgincludedir)/_gen

noinst_LTLIBRARIES = libtelepathy-glib.la

tpginclude_HEADERS = \
    telepathy-glib/tp-channel-iface.h \
    telepathy-glib/tp-channel-factory-iface.h \
    telepathy-glib/tp-enums.h \
    telepathy-glib/tp-errors.h \
    telepathy-glib/tp-handle.h \
    telepathy-glib/tp-handle-repo.h \
    telepathy-glib/tp-handle-repo-static.h \
    telepathy-glib/tp-handle-repo-dynamic.h \
    telepathy-glib/tp-heap.h \
    telepathy-glib/tp-helpers.h \
    telepathy-glib/tp-interfaces.h \
    telepathy-glib/tp-intset.h

specinclude_HEADERS = \
    telepathy-glib/_spec/telepathy-enums.h \
    telepathy-glib/_spec/telepathy-errors.h \
    telepathy-glib/_spec/telepathy-interfaces.h

nodist_libtelepathy_glib_la_SOURCES = \
    _gen/tp-channel-factory-iface-signals-marshal.c \
    _gen/tp-channel-factory-iface-signals-marshal.h \
    _gen/enumtypes-spec-telepathy-errors.c

nodist_geninclude_HEADERS = \
    telepathy-glib/_gen/enumtypes-spec-telepathy-errors.h

BUILT_SOURCES = $(nodist_geninclude_HEADERS) \
		$(nodist_libtelepathy_glib_la_SOURCES)
CLEANFILES = $(BUILT_SOURCES)

libtelepathy_glib_la_SOURCES = \
    errors.c \
    handle-repo.c \
    handle-repo-dynamic.c \
    handle-repo-static.c \
    handle-set.c \
    heap.c \
    helpers.c \
    intset.c \
    tp-channel-iface.c \
    tp-channel-factory-iface.c \
    tp-channel-factory-iface-signals-marshal.list

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @HANDLE_LEAK_DEBUG_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@

_gen/%-signals-marshal.h: %-signals-marshal.list
	$(INSTALL) -d _gen
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > _gen/$*-signals-marshal.h

_gen/%-signals-marshal.c: %-signals-marshal.list
	$(INSTALL) -d _gen
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > _gen/$*-signals-marshal.c

_gen/enumtypes-spec-%.c: telepathy-glib/_spec/%.h Makefile.in
	$(INSTALL) -d _gen
	glib-mkenums \
	--fhead "#include <telepathy-glib/_spec/$*.h>\n#include <telepathy-glib/_gen/enumtypes-spec-$*.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@VALUENAME@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
        $< > $@

telepathy-glib/_gen/enumtypes-spec-%.h: telepathy-glib/_spec/%.h Makefile.in
	$(INSTALL) -d telepathy-glib/_gen
	glib-mkenums \
	--fhead "#ifndef __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n#define __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define $(shell echo $* | tr [:lower:]- [:upper:]_ | sed 's/_.*//')_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__ */" \
        $< > $@
