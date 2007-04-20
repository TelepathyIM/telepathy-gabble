BUILT_SOURCES = \
    gabble-media-session-enumtypes.h \
    gabble-media-session-enumtypes.c \
    gabble-signals-marshal.h \
    gabble-signals-marshal.c \
    gabble-signals-marshal.list

CLEANFILES = $(BUILT_SOURCES)

bin_PROGRAMS=telepathy-gabble
noinst_PROGRAMS = write-mgr-file

# There is no point to build tube support if we don't have D-Bus tube
# as currently it's the only tube type implemented.
if HAVE_DBUS_TUBE
dbus_tube_sources = \
    tube-dbus.h \
    tube-dbus.c \
    tubes-channel.h \
    tubes-channel.c \
    tubes-factory.h \
    tubes-factory.c
endif

# These are the bits we wrote, so we can be really strict about coding style
libgabble_convenience_la_our_sources = \
    $(dbus_tube_sources) \
    base64.h \
    base64.c \
    capabilities.h \
    capabilities.c \
    bytestream-factory.h \
    bytestream-factory.c \
    bytestream-ibb.h \
    bytestream-ibb.c \
    conn-aliasing.h \
    conn-aliasing.c \
    conn-avatars.h \
    conn-avatars.c \
    conn-olpc.h \
    conn-olpc.c \
    conn-presence.h \
    conn-presence.c \
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
    gabble-register.c \
    gabble-register.h \
    gabble-roster-channel.h \
    gabble-roster-channel.c \
    gabble-roomlist-channel.h \
    gabble-roomlist-channel.c \
    gabble-types.h \
    im-factory.h \
    im-factory.c \
    media-factory.h \
    media-factory.c \
    muc-factory.h \
    muc-factory.c \
    namespaces.h \
    pubsub.h \
    pubsub.c \
    presence.h \
    presence.c \
    presence-cache.h \
    presence-cache.c \
    request-pipeline.h \
    request-pipeline.c \
    roster.h \
    roster.c \
    text-mixin.h \
    text-mixin.c \
    util.h \
    util.c \
    vcard-manager.h \
    vcard-manager.c

# we don't want to subject third-party source to whitespace removal -
# it's more useful to keep it a verbatim copy
libgabble_convenience_la_SOURCES = \
    libmd5-rfc/md5.c \
    libmd5-rfc/md5.h \
    sha1/sha1.h \
    sha1/sha1.c \
    $(libgabble_convenience_la_our_sources)

libgabble_convenience_la_LIBADD = \
   $(top_builddir)/extensions/libgabble-extensions.la

nodist_libgabble_convenience_la_SOURCES = \
    $(BUILT_SOURCES)

write_mgr_file_SOURCES = write-mgr-file.c

write_mgr_file_LDADD = libgabble-convenience.la

telepathy_gabble_SOURCES = \
    gabble.h \
    gabble.c

check_c_sources = \
    $(telepathy_gabble_SOURCES) \
    $(libgabble_convenience_la_our_sources) \
    $(telepathy_gabble_SOURCES) \
    $(write_mgr_file_SOURCES)
include $(top_srcdir)/check-coding-style.mk

telepathy_gabble_LDADD = libgabble-convenience.la

noinst_LTLIBRARIES = libgabble-convenience.la

AM_CFLAGS = $(ERROR_CFLAGS) -I$(top_srcdir) -I$(top_builddir) \
	    @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@ \
	    @HANDLE_LEAK_DEBUG_CFLAGS@ @COVERAGE_CFLAGS@ @TP_GLIB_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @LOUDMOUTH_LIBS@ @TP_GLIB_LIBS@


gabble-signals-marshal.list: $(libgabble_convenience_la_SOURCES) Makefile.am
	( cd $(srcdir) && \
	sed -n -e 's/.*gabble_marshal_\([[:upper:]]*__[[:upper:]_]*\).*/\1/p' \
	$(libgabble_convenience_la_SOURCES) ) \
	| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@.tmp
	if cmp -s $@.tmp $@; then \
		rm $@.tmp; \
	else \
		mv $@.tmp $@; \
	fi

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
