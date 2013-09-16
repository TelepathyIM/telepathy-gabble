BUILT_SOURCES = \
    gabble-signals-marshal.h \
    gabble-signals-marshal.c \
    gabble-signals-marshal.list \
    gabble-enumtypes.h \
    gabble-enumtypes.c

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST =

libexec_PROGRAMS=telepathy-gabble
noinst_PROGRAMS = write-mgr-file

libgabble_convenience_la_SOURCES = \
    addressing-util.h \
    addressing-util.c \
    auth-manager.h \
    auth-manager.c \
    bytestream-factory.h \
    bytestream-factory.c \
    bytestream-ibb.h \
    bytestream-ibb.c \
    bytestream-iface.h \
    bytestream-iface.c \
    bytestream-muc.h \
    bytestream-muc.c \
    bytestream-multiple.h \
    bytestream-multiple.c \
    bytestream-socks5.h \
    bytestream-socks5.c \
    capabilities.c \
    caps-hash.h \
    caps-hash.c \
    caps-channel-manager.c \
    conn-addressing.h \
    conn-addressing.c \
    conn-aliasing.h \
    conn-aliasing.c \
    conn-avatars.h \
    conn-avatars.c \
    conn-client-types.h \
    conn-client-types.c \
    conn-contact-info.h \
    conn-contact-info.c \
    conn-location.h \
    conn-location.c \
    conn-olpc.h \
    conn-olpc.c \
    conn-power-saving.h \
    conn-power-saving.c \
    conn-presence.h \
    conn-presence.c \
    conn-sidecars.h \
    conn-sidecars.c \
    conn-util.h \
    conn-util.c \
    conn-mail-notif.h \
    conn-mail-notif.c \
    connection.h \
    connection.c \
    connection-manager.h \
    connection-manager.c \
    debug.h \
    debug.c \
    disco.h \
    disco.c \
    error.c \
    error.h \
    gabble.c \
    gabble.h \
    im-channel.h \
    im-channel.c \
    im-factory.h \
    im-factory.c \
    message-util.h \
    message-util.c \
    muc-channel.h \
    muc-channel.c \
    muc-factory.h \
    muc-factory.c \
    muc-tube-dbus.h \
    muc-tube-dbus.c \
    muc-tube-stream.h \
    muc-tube-stream.c \
    namespaces.h \
    olpc-activity.h \
    olpc-activity.c \
    plugin-loader.h \
    plugin-loader.c \
    presence.h \
    presence.c \
    presence-cache.h \
    presence-cache.c \
    protocol.h \
    protocol.c \
    private-tubes-factory.h \
    private-tubes-factory.c \
    request-pipeline.h \
    request-pipeline.c \
    roster.h \
    roster.c \
    room-config.h \
    room-config.c \
    roomlist-channel.h \
    roomlist-channel.c \
    roomlist-manager.h \
    roomlist-manager.c \
    search-channel.h \
    search-channel.c \
    search-manager.h \
    search-manager.c \
    server-sasl-channel.h \
    server-sasl-channel.c \
    server-tls-channel.h \
    server-tls-channel.c \
    server-tls-manager.h \
    server-tls-manager.c \
    sidecar.c \
    tls-certificate.h \
    tls-certificate.c \
    tube-iface.h \
    tube-iface.c \
    tube-dbus.h \
    tube-dbus.c \
    tube-stream.h \
    tube-stream.c \
    types.h \
    util.h \
    util.c \
    vcard-manager.h \
    vcard-manager.c

if ENABLE_FILE_TRANSFER
libgabble_convenience_la_SOURCES += \
    ft-channel.c \
    ft-channel.h \
    ft-manager.c \
    ft-manager.h
endif

if ENABLE_VOIP
libgabble_convenience_la_SOURCES += \
    base-call-channel.h \
    base-call-channel.c \
    call-content.h \
    call-content.c \
    call-channel.h \
    call-channel.c \
    call-muc-channel.h \
    call-muc-channel.c \
    call-member.h \
    call-member.c \
    call-member-content.h \
    call-member-content.c \
    call-stream.h \
    call-stream.c \
    jingle-share.h \
    jingle-share.c \
    jingle-mint.h \
    jingle-mint.c \
    jingle-tp-util.h \
    jingle-tp-util.c \
    media-factory.h \
    media-factory.c
endif

if ENABLE_JINGLE_FILE_TRANSFER
libgabble_convenience_la_SOURCES += \
    gtalk-file-collection.c \
    gtalk-file-collection.h
endif

enumtype_sources = \
    $(top_srcdir)/src/connection.h \
    $(top_srcdir)/src/room-config.h \
    $(top_srcdir)/src/presence.h

libgabble_convenience_la_LIBADD = \
   $(top_builddir)/extensions/libgabble-extensions.la \
   $(top_builddir)/lib/gibber/libgibber.la \
   libgabble-plugins.la \
   $(ALL_LIBS)

nodist_libgabble_convenience_la_SOURCES = \
    $(BUILT_SOURCES)

write_mgr_file_SOURCES = write-mgr-file.c

write_mgr_file_LDADD = libgabble-convenience.la

telepathy_gabble_SOURCES = \
    main.c

check_c_sources = \
    $(telepathy_gabble_SOURCES) \
    $(libgabble_convenience_la_SOURCES) \
    $(write_mgr_file_SOURCES)
include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style

telepathy_gabble_LDADD = libgabble-convenience.la

telepathy_gabble_LDFLAGS = -export-dynamic

noinst_LTLIBRARIES = libgabble-convenience.la

pluginexeclib_LTLIBRARIES = libgabble-plugins.la

# Gabble's plugin API is not stable yet (it can't be, since neither is Wocky),
# so use -release to make the SONAME of the plugin library change with every
# Gabble release.
libgabble_plugins_la_LDFLAGS = \
   -no-undefined \
   -release $(VERSION) \
   $(NULL)

libgabble_plugins_la_LIBADD = \
   $(ALL_LIBS)

libgabble_plugins_la_SOURCES = \
   capabilities.c \
   caps-channel-manager.c \
   debug.c \
   error.c \
   plugin.c \
   plugin-connection.c \
   sidecar.c

AM_CFLAGS = $(ERROR_CFLAGS) -I$(top_srcdir) -I$(top_builddir) \
	    @DBUS_CFLAGS@ @GLIB_CFLAGS@ @WOCKY_CFLAGS@ \
	    @TP_GLIB_CFLAGS@ \
	    @SOUP_CFLAGS@ @NICE_CFLAGS@ @GMODULE_CFLAGS@ \
	    -I $(top_srcdir)/lib -I $(top_builddir)/lib \
	    -DG_LOG_DOMAIN=\"gabble\" \
	    -DPLUGIN_DIR=\"$(pluginexecdir)\"

# following flag is requied to make getnameinfo work
if WINDOWS
  AM_CFLAGS += -D_WIN32_WINNT=0x0501
endif

ALL_LIBS =  @DBUS_LIBS@ @GLIB_LIBS@ @WOCKY_LIBS@ @TP_GLIB_LIBS@ \
	    @SOUP_LIBS@ @NICE_LIBS@ @GMODULE_LIBS@

# build gibber first
all: gibber
gibber:
	@${MAKE} -C $(top_builddir)/lib/gibber libgibber.la
.PHONY: gibber

gabble-signals-marshal.list: $(libgabble_convenience_la_SOURCES) Makefile.am
	@( cd $(srcdir) && \
	sed -n -e 's/.*gabble_marshal_\([[:upper:][:digit:]]*__[[:upper:][:digit:]_]*\).*/\1/p' \
	$(libgabble_convenience_la_SOURCES) ) \
	| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@.tmp
	@if cmp -s $@.tmp $@; then \
		rm $@.tmp; \
	else \
		mv $@.tmp $@; \
	fi

%-signals-marshal.h: %-signals-marshal.list Makefile.am
	$(AM_V_GEN)glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $@

%-signals-marshal.c: %-signals-marshal.list Makefile.am
	$(AM_V_GEN){ echo '#include "$*-signals-marshal.h"' && \
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< ; \
	} > $@

# rules for making the glib enum objects
gabble-enumtypes.h: $(enumtype_sources) Makefile.in
	$(AM_V_GEN)glib-mkenums \
	--fhead "#ifndef __GABBLE_ENUM_TYPES_H__\n#define __GABBLE_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define GABBLE_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __GABBLE_ENUM_TYPES_H__ */" \
	$(enumtype_sources) > $@

gabble-enumtypes.c: $(enumtype_sources) Makefile.in
	$(AM_V_GEN)glib-mkenums \
	--fhead "#include \"config.h\"\n" \
	--fhead "#include <$*.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */\n#include \"@filename@\"" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$(enumtype_sources) > $@

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer -:PROJECT telepathy-gabble \
	-:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	-:SHARED gabble-plugins -:TAGS eng debug \
	-:SOURCES $(libgabble_plugins_la_SOURCES) \
	-:CFLAGS $(DEFS) $(CFLAGS) $(DEFAULT_INCLUDES) $(INCLUDES) \
	$(AM_CFLAGS) \
	-:CPPFLAGS $(CPPFLAGS) $(AM_CPPFLAGS) \
	-:LDFLAGS $(libgabble_plugins_la_LDFLAGS) \
	$(libgabble_plugins_la_LIBADD) \
	-:SHARED telepathy-gabble -:TAGS eng debug \
	-:SOURCES $(libgabble_convenience_la_SOURCES) \
	$(nodist_libgabble_convenience_la_SOURCES) main.c \
	-:CFLAGS $(DEFS) $(CFLAGS) $(DEFAULT_INCLUDES) $(INCLUDES) \
	$(AM_CFLAGS) -DBUILD_AS_ANDROID_SERVICE \
	-:CPPFLAGS $(CPPFLAGS) $(AM_CPPFLAGS) $(telepathy_gabble_LDFLAGS) \
	-:LDFLAGS $(telepathy_gabble_LDADD) $(libgabble_convenience_la_LIBADD) \
	$(lib_LTLIBRARIES) \
	> $@
