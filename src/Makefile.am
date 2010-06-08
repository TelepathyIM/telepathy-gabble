BUILT_SOURCES = \
    gabble-signals-marshal.h \
    gabble-signals-marshal.c \
    gabble-signals-marshal.list

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST =

libexec_PROGRAMS=telepathy-gabble
noinst_PROGRAMS = write-mgr-file

libgabble_convenience_la_SOURCES = \
    auth-manager.h \
    auth-manager.c \
    base64.h \
    base64.c \
    base-channel.h \
    base-channel.c \
    base-call-channel.h \
    base-call-channel.c \
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
    call-content.h \
    call-content.c \
    call-content-codecoffer.h \
    call-content-codecoffer.c \
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
    call-stream-endpoint.h \
    call-stream-endpoint.c \
    capabilities.h \
    capabilities.c \
    caps-cache.h \
    caps-cache.c \
    caps-hash.h \
    caps-hash.c \
    caps-channel-manager.h \
    caps-channel-manager.c \
    conn-aliasing.h \
    conn-aliasing.c \
    conn-avatars.h \
    conn-avatars.c \
    conn-contact-info.h \
    conn-contact-info.c \
    conn-location.h \
    conn-location.c \
    conn-olpc.h \
    conn-olpc.c \
    conn-presence.h \
    conn-presence.c \
    conn-sidecars.h \
    conn-sidecars.c \
    conn-slacker.h \
    conn-slacker.c \
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
    ft-channel.c \
    ft-channel.h \
    ft-manager.c \
    ft-manager.h \
    gabble.c \
    gabble.h \
    gtalk-file-collection.h \
    gtalk-file-collection.c \
    im-channel.h \
    im-channel.c \
    im-factory.h \
    im-factory.c \
    jingle-content.h \
    jingle-content.c \
    jingle-factory.h \
    jingle-factory.c \
    jingle-share.h \
    jingle-share.c \
    jingle-media-rtp.h \
    jingle-media-rtp.c \
    jingle-session.h \
    jingle-session.c \
    jingle-transport-google.h \
    jingle-transport-google.c \
    jingle-transport-rawudp.h \
    jingle-transport-rawudp.c \
    jingle-transport-iceudp.h \
    jingle-transport-iceudp.c \
    jingle-transport-iface.h \
    jingle-transport-iface.c \
    media-channel.h \
    media-channel-internal.h \
    media-channel.c \
    media-channel-hold.c \
    media-stream.h \
    media-stream.c \
    media-factory.h \
    media-factory.c \
    message-util.h \
    message-util.c \
    muc-channel.h \
    muc-channel.c \
    muc-factory.h \
    muc-factory.c \
    namespaces.h \
    olpc-activity.h \
    olpc-activity.c \
    olpc-activity-view.h \
    olpc-activity-view.c \
    olpc-buddy-view.h \
    olpc-buddy-view.c \
    olpc-gadget-manager.h \
    olpc-gadget-manager.c \
    olpc-view.h \
    olpc-view.c \
    plugin-loader.h \
    plugin-loader.c \
    $(top_srcdir)/gabble/plugin.h \
    plugin.c \
    presence.h \
    presence.c \
    presence-cache.h \
    presence-cache.c \
    private-tubes-factory.h \
    private-tubes-factory.c \
    request-pipeline.h \
    request-pipeline.c \
    roster.h \
    roster.c \
    roster-channel.h \
    roster-channel.c \
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
    $(top_srcdir)/gabble/sidecar.h \
    sidecar.c \
    slacker.h \
    slacker.c \
    tube-iface.h \
    tube-iface.c \
    tubes-channel.h \
    tubes-channel.c \
    tube-dbus.h \
    tube-dbus.c \
    tube-stream.h \
    tube-stream.c \
    types.h \
    util.h \
    util.c \
    vcard-manager.h \
    vcard-manager.c

libgabble_convenience_la_LIBADD = \
   $(top_builddir)/extensions/libgabble-extensions.la \
   $(top_builddir)/lib/gibber/libgibber.la \
   $(top_builddir)/lib/loudmouth/libloudmouth.la \
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

AM_CFLAGS = $(ERROR_CFLAGS) -I$(top_srcdir) -I$(top_builddir) \
	    @DBUS_CFLAGS@ @GLIB_CFLAGS@ @WOCKY_CFLAGS@ \
	    @HANDLE_LEAK_DEBUG_CFLAGS@ @TP_GLIB_CFLAGS@ \
	    @SOUP_CFLAGS@ @NICE_CFLAGS@ @UUID_CFLAGS@ @GMODULE_CFLAGS@ \
	    @SQLITE_CFLAGS@ \
	    -I $(top_srcdir)/lib -I $(top_builddir)/lib \
	    -I $(top_srcdir)/gabble \
	    -DG_LOG_DOMAIN=\"gabble\" \
	    -DPLUGIN_DIR=\"$(libdir)/telepathy/gabble-0\"

ALL_LIBS =  @DBUS_LIBS@ @GLIB_LIBS@ @WOCKY_LIBS@ @TP_GLIB_LIBS@ \
	    @SOUP_LIBS@ @NICE_LIBS@ @UUID_LIBS@ @GMODULE_LIBS@ @SQLITE_LIBS@

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
