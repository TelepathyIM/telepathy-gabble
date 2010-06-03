EXTRA_DIST = \
    generate-wockyinclude_HEADERS

if ENABLE_PLUGIN_API
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = telepathy-gabble.pc

gabbleincludedir = $(includedir)/telepathy-gabble-0/gabble
gabbleinclude_HEADERS = \
    capabilities-set.h \
    connection.h \
    error.h \
    gabble.h \
    plugin.h \
    sidecar.h \
    types.h

# We install these headers alongside gabble's so that an actual separate Wocky
# installation won't clash with them. Update with:
#   % ./gabble/generate-wockyinclude_HEADERS
wockyincludedir = $(includedir)/telepathy-gabble-0/wocky
wockyinclude_HEADERS = \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-types.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-connector.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-xmpp-connection.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-debug.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-namespaces.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-node-tree.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-utils.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-node.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-node-private.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-xmpp-reader.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-xmpp-writer.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-xmpp-error.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-sasl-auth.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-sasl-digest-md5.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-auth-handler.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-sasl-plain.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-stanza.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-porter.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-contact.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-bare-contact.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-resource-contact.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-roster.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-contact-factory.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-session.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-muc.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-pep-service.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-data-form.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-pubsub-helpers.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-pubsub-service.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-pubsub-service-protected.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-pubsub-node.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-pubsub-node-protected.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-auth-registry.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-jabber-auth.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-jabber-auth-digest.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-jabber-auth-password.h \
    $(top_srcdir)/lib/ext/wocky/wocky/wocky-ping.h

endif
