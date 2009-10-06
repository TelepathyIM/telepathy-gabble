/*
 * gabble-connection.h - Header for GabbleConnection
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __GABBLE_CONNECTION_H__
#define __GABBLE_CONNECTION_H__

#include <dbus/dbus-glib.h>
#include <glib-object.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/contacts-mixin.h>
#include <telepathy-glib/presence-mixin.h>
#include <telepathy-glib/dbus-properties-mixin.h>

#include "error.h"
#include "ft-manager.h"
#include "jingle-factory.h"
#include "muc-factory.h"
#include "olpc-gadget-manager.h"
#include "types.h"

G_BEGIN_DECLS

/* Default parameters for optional parameters */
#define GABBLE_PARAMS_DEFAULT_HTTPS_PROXY_PORT           443
#define GABBLE_PARAMS_DEFAULT_STUN_PORT                  3478
#define GABBLE_PARAMS_DEFAULT_FALLBACK_STUN_SERVER       "stun.collabora.co.uk"
/* List of public SOCKS5 proxies:
 * http://coccinella.im/servers/servers_by_proxy_bytestreams.html */
#define GABBLE_PARAMS_DEFAULT_SOCKS5_PROXIES             \
    { "proxy.jabber.org", "proxy.jabberfr.org",\
      "proxy65.rooyee.biz", "proxy.jabbim.cz",\
      "proxy.911910.cn",\
      "proxy.downtempo.de",\
      "proxy.im.flosoft.biz",\
      "proxy.jabber.bluendo.com", "proxy.jabber.dk", "proxy.jabber.freenet.de",\
      "proxy.fsinf.at", "proxy.jabber.minus273.org",\
      "proxy.jabber.planetteamspeak.com", "proxy.jabber.tf-network.de",\
      "proxy.jabjab.de", "proxy.jabster.pl",\
      "proxy.schokokeks.org",\
      "proxy.ubuntu-jabber.de", "proxy.ubuntu-jabber.net",\
      "proxy65.unstable.nl", "proxy.verdammung.org", "proxy.vke.ru",\
      "proxy.vodka-pomme.net", "proxy.jabbernet.eu",\
      NULL }

/* These proxies appeared to be the same as proxy.jabbernet.eu
    "proxy.jabberchat.eu", "proxy.shady.nl", "proxy.nedbsd.be",\
    "proxy.nedbsd.eu", "proxy.nedbsd.nl", "proxy.4business.nl"\
*/

/* order must match array of statuses in conn-presence.c */
/* in increasing order of presence */
typedef enum
{
  GABBLE_PRESENCE_OFFLINE = 0,
  GABBLE_PRESENCE_UNKNOWN,
  GABBLE_PRESENCE_ERROR,
  GABBLE_PRESENCE_LAST_UNAVAILABLE = GABBLE_PRESENCE_ERROR,
  GABBLE_PRESENCE_HIDDEN,
  GABBLE_PRESENCE_XA,
  GABBLE_PRESENCE_AWAY,
  GABBLE_PRESENCE_DND,
  GABBLE_PRESENCE_AVAILABLE,
  GABBLE_PRESENCE_CHAT,
  NUM_GABBLE_PRESENCES
} GabblePresenceId;

typedef enum
{
  GABBLE_CONNECTION_FEATURES_NONE = 0,
  GABBLE_CONNECTION_FEATURES_GOOGLE_JINGLE_INFO = 1 << 0,
  GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER = 1 << 1,
  GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE = 1 << 2,
  GABBLE_CONNECTION_FEATURES_PRIVACY = 1 << 3,
  GABBLE_CONNECTION_FEATURES_PEP = 1 << 4,
} GabbleConnectionFeatures;

typedef struct _GabbleConnectionPrivate GabbleConnectionPrivate;
typedef struct _GabbleConnectionClass GabbleConnectionClass;

typedef LmHandlerResult (*GabbleConnectionMsgReplyFunc) (GabbleConnection *conn,
                                                         LmMessage *sent_msg,
                                                         LmMessage *reply_msg,
                                                         GObject *object,
                                                         gpointer user_data);

/* must be in the same order as the list_handle_strings in
 * connection.c */
typedef enum
{
  GABBLE_LIST_HANDLE_PUBLISH = 1,
  GABBLE_LIST_HANDLE_SUBSCRIBE,
  GABBLE_LIST_HANDLE_STORED,
  GABBLE_LIST_HANDLE_DENY
} GabbleListHandle;

typedef enum {
    /* The JID could be a "global" JID, or a MUC room member. We'll assume
     * that it's a global JID (and remove the resource) unless we've seen
     * that JID in a MUC before.
     */
    GABBLE_JID_ANY = 0,
    /* The JID is definitely global. Remove the resource. */
    GABBLE_JID_GLOBAL,
    /* The JID is definitely a room member. Assert that there is a "resource"
     * (nickname) and don't remove it. */
    GABBLE_JID_ROOM_MEMBER
} GabbleNormalizeContactJIDMode;

struct _GabbleConnectionClass {
    TpBaseConnectionClass parent_class;
    TpDBusPropertiesMixinClass properties_class;
    TpPresenceMixinClass presence_class;
    TpContactsMixinClass contacts_class;
};

struct _GabbleConnection {
    TpBaseConnection parent;
    TpPresenceMixin presence;
    TpContactsMixin contacts;

    /* loudmouth connection */
    LmConnection *lmconn;

    /* channel factories borrowed from TpBaseConnection's list */
    GabbleRoster *roster;
    GabbleMucFactory *muc_factory;
    GabblePrivateTubesFactory *private_tubes_factory;

    /* DISCO! */
    GabbleDisco *disco;

    /* connection feature flags */
    GabbleConnectionFeatures features;

    /* presence */
    GabblePresenceCache *presence_cache;
    GabblePresence *self_presence;

    /* IQ request pipeline helper, so simultaneous requests don't make
     * servers hate us */
    GabbleRequestPipeline *req_pipeline;

    /* vCard lookup helper */
    GabbleVCardManager *vcard_manager;

    /* OLPC hash tables */
    GHashTable *olpc_activities_info;
    GHashTable *olpc_pep_activities;
    GHashTable *olpc_invited_activities;
    GHashTable *olpc_current_act;

    /* OLPC services */
    const gchar *olpc_gadget_buddy;
    const gchar *olpc_gadget_activity;
    gboolean olpc_gadget_publish;

    /* OLPC Gadget manager */
    GabbleOlpcGadgetManager *olpc_gadget_manager;

    /* bytestream factory */
    GabbleBytestreamFactory *bytestream_factory;

    /* outstanding avatar requests */
    GHashTable *avatar_requests;

    /* jingle factory */
    GabbleJingleFactory *jingle_factory;

    /* file transfer manager */
    GabbleFtManager *ft_manager;

    /* temporary, for requestotron support */
    GPtrArray *channel_factories;
    GPtrArray *channel_managers;
    GPtrArray *channel_requests;
    gboolean has_tried_connection;

    GabbleConnectionPrivate *priv;
};

typedef enum {
    GABBLE_CONNECTION_ALIAS_NONE = 0,
    GABBLE_CONNECTION_ALIAS_FROM_JID,
    GABBLE_CONNECTION_ALIAS_FROM_VCARD,
    GABBLE_CONNECTION_ALIAS_FROM_MUC_RESOURCE,
    GABBLE_CONNECTION_ALIAS_FROM_CONNMGR,
    GABBLE_CONNECTION_ALIAS_FROM_PRESENCE,
    GABBLE_CONNECTION_ALIAS_FROM_ROSTER
} GabbleConnectionAliasSource;

GType gabble_connection_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_CONNECTION \
  (gabble_connection_get_type ())
#define GABBLE_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_CONNECTION, GabbleConnection))
#define GABBLE_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_CONNECTION, \
                           GabbleConnectionClass))
#define GABBLE_IS_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_CONNECTION))
#define GABBLE_IS_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_CONNECTION))
#define GABBLE_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_CONNECTION, \
                              GabbleConnectionClass))

gchar *gabble_connection_get_full_jid (GabbleConnection *conn);

gboolean _gabble_connection_set_properties_from_account (
    GabbleConnection *conn, const gchar *account, GError **error);
gboolean _gabble_connection_send (GabbleConnection *conn, LmMessage *msg,
    GError **error);
gboolean _gabble_connection_send_with_reply (GabbleConnection *conn,
    LmMessage *msg, GabbleConnectionMsgReplyFunc reply_func, GObject *object,
    gpointer user_data, GError **error);
void _gabble_connection_acknowledge_set_iq (GabbleConnection *conn,
    LmMessage *iq);
void _gabble_connection_send_iq_error (GabbleConnection *conn,
    LmMessage *message, GabbleXmppError error, const gchar *errmsg);

const char *_gabble_connection_find_conference_server (GabbleConnection *);
gboolean _gabble_connection_signal_own_presence (GabbleConnection *,
    GError **);

void gabble_connection_ensure_capabilities (GabbleConnection *conn,
    GabblePresenceCapabilities caps);

gboolean gabble_connection_send_presence (GabbleConnection *conn,
    LmMessageSubType sub_type, const gchar *contact, const gchar *status,
    GError **error);

/* extern only for the benefit of the unit tests */
void _gabble_connection_create_handle_repos (TpBaseConnection *conn,
    TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES]);

/* For unit tests only */
void gabble_connection_set_connect_timeout (guint timeout);
void gabble_connection_set_disco_reply_timeout (guint timeout);

G_END_DECLS

#endif /* #ifndef __GABBLE_CONNECTION_H__*/
