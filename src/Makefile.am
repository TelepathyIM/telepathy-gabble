BUILT_SOURCES = \
    gabble-connection-manager-glue.h \
    gabble-connection-manager-signals-marshal.h \
    gabble-connection-manager-signals-marshal.c \
    gabble-connection-glue.h \
    gabble-connection-signals-marshal.h \
    gabble-connection-signals-marshal.c \
    gabble-register-signals-marshal.h \
    gabble-register-signals-marshal.c \
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
    gabble-media-session-enumtypes.h \
    gabble-media-session-enumtypes.c \
    gabble-media-stream-glue.h \
    gabble-media-stream-signals-marshal.h \
    gabble-media-stream-signals-marshal.c \
    gabble-presence-cache-signals-marshal.h \
    gabble-presence-cache-signals-marshal.c \
    gabble-roster-channel-glue.h \
    gabble-roster-channel-signals-marshal.h \
    gabble-roster-channel-signals-marshal.c \
    gabble-roomlist-channel-glue.h \
    gabble-roomlist-channel-signals-marshal.h \
    gabble-roomlist-channel-signals-marshal.c \
    group-mixin-signals-marshal.h \
    group-mixin-signals-marshal.c \
    properties-mixin-signals-marshal.h \
    properties-mixin-signals-marshal.c \
    telepathy-spec-errors-enumtypes.h \
    telepathy-spec-errors-enumtypes.c \
    text-mixin-signals-marshal.h \
    text-mixin-signals-marshal.c \
    tp-channel-factory-iface-signals-marshal.h \
    tp-channel-factory-iface-signals-marshal.c

CLEANFILES = $(BUILT_SOURCES)

bin_PROGRAMS=telepathy-gabble
noinst_PROGRAMS = write-mgr-file

libgabble_convenience_la_SOURCES = \
    ansi.h \
    base64.h \
    base64.c \
    capabilities.h \
    capabilities.c \
    debug.h \
    debug.c \
    disco.h \
    disco.c \
    gabble-error.c \
    gabble-error.h \
    gabble-connection-manager.h \
    gabble-connection-manager.c \
    gabble-connection.h \
    gabble-connection.c \
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
    gabble-presence.h \
    gabble-presence.c \
    gabble-presence-cache.h \
    gabble-presence-cache.c \
    gabble-register.c \
    gabble-register.h \
    gabble-roster-channel.h \
    gabble-roster-channel.c \
    gabble-roomlist-channel.h \
    gabble-roomlist-channel.c \
    gabble-types.h \
    group-mixin.h \
    group-mixin.c \
    handles.h \
    handles.c \
    handle-set.h \
    handle-set.c \
    jingle-info.c \
    jingle-info.h \
    im-factory.h \
    im-factory.c \
    media-factory.h \
    media-factory.c \
    muc-factory.h \
    muc-factory.c \
    namespaces.h \
    properties-mixin.h \
    properties-mixin.c \
    roster.h \
    roster.c \
    sha1/sha1.h \
    sha1/sha1.c \
    telepathy-constants.h \
    telepathy-interfaces.h \
    telepathy-errors.h \
    telepathy-errors.c \
    telepathy-helpers.h \
    telepathy-helpers.c \
    text-mixin.h \
    text-mixin.c \
    tp-channel-iface.h \
    tp-channel-iface.c \
    tp-channel-factory-iface.h \
    tp-channel-factory-iface.c \
    util.h \
    util.c \
    vcard-manager.h \
    vcard-manager.c \
    gabble-connection-manager-signals-marshal.list \
    gabble-connection-signals-marshal.list \
    gabble-im-channel-signals-marshal.list \
    gabble-media-channel-signals-marshal.list \
    gabble-media-session-signals-marshal.list \
    gabble-media-stream-signals-marshal.list \
    gabble-muc-channel-signals-marshal.list \
    gabble-presence-cache-signals-marshal.list \
    gabble-register-signals-marshal.list \
    gabble-roomlist-channel-signals-marshal.list \
    gabble-roster-channel-signals-marshal.list \
    group-mixin-signals-marshal.list \
    properties-mixin-signals-marshal.list \
    text-mixin-signals-marshal.list \
    tp-channel-factory-iface-signals-marshal.list

libgabble_convenience_la_LIBADD = ../lib/libtelepathy-glib.la

nodist_libgabble_convenience_la_SOURCES = \
    $(BUILT_SOURCES)

write_mgr_file_SOURCES = write-mgr-file.c

write_mgr_file_LDADD = libgabble-convenience.la

telepathy_gabble_SOURCES = \
    gabble.h \
    gabble.c

telepathy_gabble_LDADD = libgabble-convenience.la

noinst_LTLIBRARIES = libgabble-convenience.la

AM_CFLAGS = $(ERROR_CFLAGS) -I$(top_srcdir)/lib @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@ @HANDLE_LEAK_DEBUG_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @LOUDMOUTH_LIBS@


# rule to generate the binding headers
%-glue.h: ../generate/xml-modified/%.xml
	dbus-binding-tool --mode=glib-server --output=$@ --prefix=$(subst -,_,$*) $<

%-signals-marshal.h: %-signals-marshal.list
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.h

%-signals-marshal.c: %-signals-marshal.list
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.c

%-marshal.h: %-marshal.list
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.h

%-marshal.c: %-marshal.list
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.c


# rules for making the glib enum objects
%-enumtypes.h: %.h Makefile.in
	glib-mkenums \
	--fhead "#ifndef __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n#define __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define $(shell echo $* | tr [:lower:]- [:upper:]_ | sed 's/_.*//')_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__ */" \
	$< > $@

%-enumtypes.c: %.h Makefile.in
	glib-mkenums \
	--fhead "#include <$*.h>\n#include <$*-enumtypes.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@VALUENAME@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$< > $@
