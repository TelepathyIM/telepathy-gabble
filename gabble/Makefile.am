if ENABLE_PLUGINS
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = telepathy-gabble.pc

gabbleincludedir = $(includedir)/telepathy-1.0/gabble
gabbleinclude_HEADERS = \
    plugin.h \
    sidecar.h

# We install these headers under gabble's so that an actual separate Wocky
# installation won't clash with them.
wockyincludedir = $(gabbleincludedir)/wocky
wockyinclude_HEADERS = \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-types.h                  \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky.h                        \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-connector.h              \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-xmpp-connection.h        \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-debug.h                  \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-namespaces.h             \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-utils.h                  \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-xmpp-node.h              \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-xmpp-reader.h            \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-xmpp-stanza.h            \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-xmpp-writer.h            \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-xmpp-error.h             \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-sasl-auth.h              \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-sasl-digest-md5.h        \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-sasl-handler.h           \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-sasl-plain.h             \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-porter.h                 \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-contact.h                \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-bare-contact.h           \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-resource-contact.h       \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-roster.h                 \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-contact-factory.h        \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-session.h                \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-pep-service.h

endif
