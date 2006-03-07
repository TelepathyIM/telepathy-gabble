BUILT_SOURCES = \
    gabble-connection-manager-glue.h \
    gabble-connection-manager-signals-marshal.h \
    gabble-connection-manager-signals-marshal.c \
    gabble-connection-glue.h \
    gabble-connection-signals-marshal.h \
    gabble-connection-signals-marshal.c \
    gabble-group-mixin-signals-marshal.h \
    gabble-group-mixin-signals-marshal.c \
    gabble-im-channel-glue.h \
    gabble-im-channel-signals-marshal.h \
    gabble-im-channel-signals-marshal.c \
    gabble-muc-channel-glue.h \
    gabble-muc-channel-signals-marshal.h \
    gabble-muc-channel-signals-marshal.c \
    gabble-media-channel-glue.h \
    gabble-media-channel-signals-marshal.h \
    gabble-media-channel-signals-marshal.c \
    gabble-media-session-glue.h \
    gabble-media-session-signals-marshal.h \
    gabble-media-session-signals-marshal.c \
    gabble-media-stream-glue.h \
    gabble-media-stream-signals-marshal.h \
    gabble-media-stream-signals-marshal.c \
    gabble-roster-channel-glue.h \
    gabble-roster-channel-signals-marshal.h \
    gabble-roster-channel-signals-marshal.c \
    telepathy-errors-enumtypes.h \
    telepathy-errors-enumtypes.c

# correctly clean the generated source files
CLEANFILES = $(BUILT_SOURCES)

bin_PROGRAMS=telepathy-gabble

CORE_SOURCES = \
    allocator.h \
    allocator.c \
    gabble-connection-manager.h \
    gabble-connection-manager.c \
    gabble-connection.h \
    gabble-connection.c \
    gabble-group-mixin.h \
    gabble-group-mixin.c \
    gabble-im-channel.h \
    gabble-im-channel.c \
    gabble-muc-channel.h \
    gabble-muc-channel.c \
    gabble-media-channel.h \
    gabble-media-channel.c \
    gabble-media-session.h \
    gabble-media-session.c \
    gabble-media-stream.h \
    gabble-media-stream.c \
    gabble-roster-channel.h \
    gabble-roster-channel.c \
    gabble-disco.h \
    gabble-disco.c \
    gintset.h \
    gintset.c \
    handles.h \
    handles.c \
    handles-private.h \
    handle-set.h \
    handle-set.c \
    telepathy-constants.h \
    telepathy-interfaces.h \
    telepathy-errors.h \
    telepathy-errors.c \
    telepathy-helpers.h \
    telepathy-helpers.c \
    $(BUILT_SOURCES)

libgabble_convenience_la_SOURCES = \
    $(CORE_SOURCES)

telepathy_gabble_SOURCES = \
    gabble.h \
    gabble.c

telepathy_gabble_LDADD = libgabble-convenience.la

noinst_LTLIBRARIES = libgabble-convenience.la

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @LOUDMOUTH_LIBS@


# rule to generate the binding headers
%-glue.h: ../generate/xml-modified/%.xml Makefile
	dbus-binding-tool --mode=glib-server --output=$@ --prefix=$(subst -,_,$*) $<

%-signals-marshal.h: %-signals-marshal.list Makefile
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.h

%-signals-marshal.c: %-signals-marshal.list Makefile
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.c

%-marshal.h: %-marshal.list Makefile
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.h

%-marshal.c: %-marshal.list Makefile
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.c


# rules for making the glib enum objects
%-enumtypes.h: %.h Makefile
	glib-mkenums \
	--fhead "#ifndef __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n#define __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define $(shell echo $* | tr [:lower:]- [:upper:]_ | sed 's/_.*//')_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__ */" \
	$< > $@

%-enumtypes.c: %.h Makefile
	glib-mkenums \
	--fhead "#include <$*.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@VALUENAME@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$< > $@
