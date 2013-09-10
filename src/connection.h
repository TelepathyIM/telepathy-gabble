/*
 * gabble-connection.h - Header for GabbleConnection
 * Copyright © 2005-2012 Collabora Ltd.
 * Copyright © 2005-2010 Nokia Corporation
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

#include "config.h"

#include <dbus/dbus-glib.h>
#include <glib-object.h>
#include <telepathy-glib/telepathy-glib.h>

#include <wocky/wocky.h>

#include "gabble/capabilities.h"
#ifdef ENABLE_FILE_TRANSFER
#include "ft-manager.h"
#endif
#ifdef ENABLE_VOIP
#include "jingle-mint.h"
#endif
#include "muc-factory.h"
#include "types.h"

#include <gabble/capabilities-set.h>
#include <gabble/types.h>
#include <gabble/plugin-connection.h>

G_BEGIN_DECLS

#define GABBLE_TYPE_CONNECTION (gabble_connection_get_type ())
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

typedef struct _GabbleConnectionClass GabbleConnectionClass;

GType gabble_connection_get_type (void);

void gabble_connection_update_sidecar_capabilities (
    GabbleConnection *connection,
    const GabbleCapabilitySet *add_set,
    const GabbleCapabilitySet *remove_set);
gchar *gabble_connection_add_sidecar_own_caps (
    GabblePluginConnection *connection,
    const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities) G_GNUC_WARN_UNUSED_RESULT;
gchar *gabble_connection_add_sidecar_own_caps_full (
    GabblePluginConnection *connection,
    const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities,
    GPtrArray *data_forms) G_GNUC_WARN_UNUSED_RESULT;

WockySession *gabble_connection_get_session (
    GabblePluginConnection *connection);

gchar *gabble_connection_get_full_jid (GabbleConnection *conn);

const gchar * gabble_connection_get_jid_for_caps (GabblePluginConnection *conn,
    WockyXep0115Capabilities *caps);

const gchar * gabble_connection_pick_best_resource_for_caps (
    GabblePluginConnection *connection,
    const gchar *jid,
    GabbleCapabilitySetPredicate predicate,
    gconstpointer user_data);

TpBaseContactList * gabble_connection_get_contact_list (
    GabbleConnection *connection);

WockyXep0115Capabilities * gabble_connection_get_caps (
    GabblePluginConnection *connection, TpHandle handle);

/* Default parameters for optional parameters */
#define GABBLE_PARAMS_DEFAULT_HTTPS_PROXY_PORT           443
#define GABBLE_PARAMS_DEFAULT_STUN_PORT                  3478
#define GABBLE_PARAMS_DEFAULT_FALLBACK_STUN_SERVER       "stun.telepathy.im"
#define GABBLE_PARAMS_DEFAULT_SOCKS5_PROXIES             { NULL }


/* order must match array of statuses in conn-presence.c */
/* in increasing order of presence */
/*< prefix=GABBLE_PRESENCE >*/
typedef enum
{
  GABBLE_PRESENCE_OFFLINE = 0,
  GABBLE_PRESENCE_UNKNOWN,
  GABBLE_PRESENCE_ERROR,
  GABBLE_PRESENCE_LAST_UNAVAILABLE = GABBLE_PRESENCE_ERROR, /*< skip >*/
  GABBLE_PRESENCE_HIDDEN,
  GABBLE_PRESENCE_XA,
  GABBLE_PRESENCE_AWAY,
  GABBLE_PRESENCE_DND,
  GABBLE_PRESENCE_AVAILABLE,
  GABBLE_PRESENCE_CHAT,
  NUM_GABBLE_PRESENCES /*< skip >*/
} GabblePresenceId;

/*< flags >*/
typedef enum
{
  GABBLE_CONNECTION_FEATURES_NONE = 0,
  GABBLE_CONNECTION_FEATURES_GOOGLE_JINGLE_INFO = 1 << 0,
  GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER = 1 << 1,
  GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE = 1 << 2,
  GABBLE_CONNECTION_FEATURES_PRIVACY = 1 << 3,
  GABBLE_CONNECTION_FEATURES_PEP = 1 << 4,
  GABBLE_CONNECTION_FEATURES_GOOGLE_MAIL_NOTIFY = 1 << 5,
  GABBLE_CONNECTION_FEATURES_INVISIBLE = 1 << 6,
  GABBLE_CONNECTION_FEATURES_GOOGLE_SHARED_STATUS = 1 << 7,
  GABBLE_CONNECTION_FEATURES_GOOGLE_QUEUE = 1 << 8,
  GABBLE_CONNECTION_FEATURES_GOOGLE_SETTING = 1 << 9,
  GABBLE_CONNECTION_FEATURES_WLM_JID_LOOKUP = 1 << 10,
} GabbleConnectionFeatures;

typedef struct _GabbleConnectionPrivate GabbleConnectionPrivate;
typedef struct _GabbleConnectionMailNotificationPrivate GabbleConnectionMailNotificationPrivate;
typedef struct _GabbleConnectionPresencePrivate GabbleConnectionPresencePrivate;

typedef void (*GabbleConnectionMsgReplyFunc) (
    GabbleConnection *conn,
    WockyStanza *sent_msg,
    WockyStanza *reply_msg,
    GObject *object,
    gpointer user_data);

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

    /* DBus daemon instance */
    TpDBusDaemon *daemon;

    WockySession *session;

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
    GabbleConnectionPresencePrivate *presence_priv;

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

    /* bytestream factory */
    GabbleBytestreamFactory *bytestream_factory;

    /* outstanding avatar requests */
    GHashTable *avatar_requests;

    /* outstanding vcard requests */
    GHashTable *vcard_requests;

#ifdef ENABLE_VOIP
    GabbleJingleMint *jingle_mint;
#endif

#ifdef ENABLE_FILE_TRANSFER
    /* file transfer manager */
    GabbleFtManager *ft_manager;
#endif

    /* PEP */
    WockyPepService *pep_nick;
    WockyPepService *pep_location;
    WockyPepService *pep_olpc_buddy_props;
    WockyPepService *pep_olpc_activities;
    WockyPepService *pep_olpc_current_act;
    WockyPepService *pep_olpc_act_props;

    /* Sidecars */
    /* gchar *interface → GabbleSidecar */
    GHashTable *sidecars;

    /* gchar *interface → GList<DBusGMethodInvocation> */
    GHashTable *pending_sidecars;

    /* Mail Notification */
    GabbleConnectionMailNotificationPrivate *mail_priv;

    /* ContactInfo.SupportedFields, or NULL to use the generic one */
    GPtrArray *contact_info_fields;

    /* Contacts' aliases from PEP. Private to conn-aliasing.c.
     * TpHandle => (transfer full) gchar *
     * We don't distinguish between "not cached" and "known to have
     * no PEP alias" here. */
    GHashTable *pep_alias_cache;

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

WockyPorter *gabble_connection_dup_porter (GabbleConnection *conn);

gboolean _gabble_connection_set_properties_from_account (
    GabbleConnection *conn, const gchar *account, GError **error);
gboolean _gabble_connection_send (GabbleConnection *conn, WockyStanza *msg,
    GError **error);
gboolean _gabble_connection_send_with_reply (GabbleConnection *conn,
    WockyStanza *msg, GabbleConnectionMsgReplyFunc reply_func, GObject *object,
    gpointer user_data, GError **error);
void _gabble_connection_acknowledge_set_iq (GabbleConnection *conn,
    WockyStanza *iq);
void gabble_connection_update_last_use (GabbleConnection *conn);

const char *_gabble_connection_find_conference_server (GabbleConnection *);
gchar *gabble_connection_get_canonical_room_name (GabbleConnection *conn,
    const gchar *jid);

void gabble_connection_ensure_capabilities (GabbleConnection *self,
    const GabbleCapabilitySet *ensured);

gboolean gabble_connection_send_presence (GabbleConnection *conn,
    WockyStanzaSubType sub_type, const gchar *contact, const gchar *status,
    GError **error);

gboolean gabble_connection_send_capabilities (GabbleConnection *self,
    const gchar *recipient, GError **error);

gboolean gabble_connection_request_decloak (GabbleConnection *self,
    const gchar *to, const gchar *reason, GError **error);

void gabble_connection_fill_in_caps (GabbleConnection *self,
    WockyStanza *presence_message);

gboolean _gabble_connection_invisible_privacy_list_set_active (
    GabbleConnection *self,
    gboolean active,
    GError **error);

const gchar **gabble_connection_get_implemented_interfaces (void);
const gchar **gabble_connection_get_guaranteed_interfaces (void);

/* extern only for the benefit of the unit tests */
void _gabble_connection_create_handle_repos (TpBaseConnection *conn,
    TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES]);

/* For unit tests only */
void gabble_connection_set_disco_reply_timeout (guint timeout);

G_END_DECLS

#endif /* #ifndef __GABBLE_CONNECTION_H__*/
