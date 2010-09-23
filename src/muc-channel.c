/*
 * gabble-muc-channel.c - Source for GabbleMucChannel
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
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

#include "config.h"
#include "muc-channel.h"

#include <stdio.h>
#include <string.h>

#include <wocky/wocky-muc.h>
#include <wocky/wocky-xmpp-error.h>

#include <dbus/dbus-glib.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/debug-ansi.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>

#include <extensions/extensions.h>

#define DEBUG_FLAG GABBLE_DEBUG_MUC
#include "connection.h"
#include "conn-aliasing.h"
#include "debug.h"
#include "disco.h"
#include "error.h"
#include "message-util.h"
#include "namespaces.h"
#include "presence.h"
#include "util.h"
#include "presence-cache.h"

#include "call-muc-channel.h"

#include "gabble-signals-marshal.h"

#define DEFAULT_JOIN_TIMEOUT 180
#define MAX_NICK_RETRIES 3

#define PROPS_POLL_INTERVAL_LOW  60 * 5
#define PROPS_POLL_INTERVAL_HIGH 60

static void password_iface_init (gpointer, gpointer);
static void chat_state_iface_init (gpointer, gpointer);
static void gabble_muc_channel_start_call_creation (GabbleMucChannel *gmuc,
    GHashTable *request);
static void muc_call_channel_finish_requests (GabbleMucChannel *self,
    GabbleCallMucChannel *call,
    GError *error);

G_DEFINE_TYPE_WITH_CODE (GabbleMucChannel, gabble_muc_channel,
    TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_PROPERTIES_INTERFACE,
      tp_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
      tp_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_PASSWORD,
      password_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT,
      tp_message_mixin_text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MESSAGES,
      tp_message_mixin_messages_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_CHAT_STATE,
      chat_state_iface_init)
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_CONFERENCE, NULL);
    )

static void gabble_muc_channel_send (GObject *obj, TpMessage *message,
    TpMessageSendingFlags flags);
static void gabble_muc_channel_close (TpBaseChannel *base);

static const gchar *gabble_muc_channel_interfaces[] = {
    TP_IFACE_CHANNEL_INTERFACE_GROUP,
    TP_IFACE_CHANNEL_INTERFACE_PASSWORD,
    TP_IFACE_PROPERTIES_INTERFACE,
    TP_IFACE_CHANNEL_INTERFACE_CHAT_STATE,
    TP_IFACE_CHANNEL_INTERFACE_MESSAGES,
    TP_IFACE_CHANNEL_INTERFACE_CONFERENCE,
    NULL
};

/* signal enum */
enum
{
    READY,
    JOIN_ERROR,
    PRE_INVITE,
    CONTACT_JOIN,
    PRE_PRESENCE,
    NEW_TUBE,

    NEW_CALL,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_STATE = 1,
  PROP_INVITED,
  PROP_INVITATION_MESSAGE,
  PROP_SELF_JID,
  PROP_WOCKY_MUC,
  PROP_TUBE,
  PROP_INITIAL_CHANNELS,
  PROP_INITIAL_INVITEE_HANDLES,
  PROP_INITIAL_INVITEE_IDS,
  PROP_ORIGINAL_CHANNELS,
  LAST_PROPERTY
};

#ifdef ENABLE_DEBUG
static const gchar *muc_states[] =
{
  "MUC_STATE_CREATED",
  "MUC_STATE_INITIATED",
  "MUC_STATE_AUTH",
  "MUC_STATE_JOINED",
  "MUC_STATE_ENDED",
};
#endif

/* role and affiliation enums */
typedef enum {
    ROLE_NONE = 0,
    ROLE_VISITOR,
    ROLE_PARTICIPANT,
    ROLE_MODERATOR,

    NUM_ROLES,

    INVALID_ROLE,
} GabbleMucRole;

typedef enum {
    AFFILIATION_NONE = 0,
    AFFILIATION_MEMBER,
    AFFILIATION_ADMIN,
    AFFILIATION_OWNER,

    NUM_AFFILIATIONS,

    INVALID_AFFILIATION,
} GabbleMucAffiliation;

/* room properties */
enum
{
  ROOM_PROP_ANONYMOUS = 0,
  ROOM_PROP_INVITE_ONLY,
  ROOM_PROP_INVITE_RESTRICTED,
  ROOM_PROP_MODERATED,
  ROOM_PROP_NAME,
  ROOM_PROP_DESCRIPTION,
  ROOM_PROP_PASSWORD,
  ROOM_PROP_PASSWORD_REQUIRED,
  ROOM_PROP_PERSISTENT,
  ROOM_PROP_PRIVATE,
  ROOM_PROP_SUBJECT,
  ROOM_PROP_SUBJECT_CONTACT,
  ROOM_PROP_SUBJECT_TIMESTAMP,

  NUM_ROOM_PROPS,

  INVALID_ROOM_PROP,
};

const TpPropertySignature room_property_signatures[NUM_ROOM_PROPS] = {
    /* Part of the room definition: modifiable by owners only */
      { "anonymous",         G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "invite-only",       G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "invite-restricted", G_TYPE_BOOLEAN },  /* impl: WRITE */
      { "moderated",         G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "name",              G_TYPE_STRING },   /* impl: READ, WRITE */

    /* Part of the room definition: might be modifiable by the owner, or
     * not at all */
      { "description",       G_TYPE_STRING },   /* impl: READ, WRITE */

    /* Part of the room definition: modifiable by owners only */
      { "password",          G_TYPE_STRING },   /* impl: WRITE */
      { "password-required", G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "persistent",        G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "private",           G_TYPE_BOOLEAN },  /* impl: READ, WRITE */

    /* fd.o#13157: currently assumed to be modifiable by everyone in the
     * room (role >= VISITOR). When that bug is fixed, it will be: */
    /* Modifiable via special <message/>s, if the user's role is high enough;
     * "high enough" is defined by the muc#roominfo_changesubject and
     * muc#roomconfig_changesubject settings. */
      { "subject",           G_TYPE_STRING },   /* impl: READ, WRITE */

    /* Special: implicitly set to "myself" and "now", respectively, by
     * changing subject. */
      { "subject-contact",   G_TYPE_UINT },     /* impl: READ */
      { "subject-timestamp", G_TYPE_UINT },     /* impl: READ */
};

/* private structures */
struct _GabbleMucChannelPrivate
{
  GabbleMucState state;

  guint join_timer_id;
  guint poll_timer_id;

  TpChannelPasswordFlags password_flags;
  DBusGMethodInvocation *password_ctx;
  gchar *password;

  const gchar *jid;
  gboolean requested;

  guint nick_retry_count;
  GString *self_jid;
  GabbleMucRole self_role;
  GabbleMucAffiliation self_affil;

  guint recv_id;

  TpPropertiesContext *properties_ctx;

  gboolean ready;
  gboolean dispose_has_run;
  gboolean invited;

  gchar *invitation_message;

  WockyMuc *wmuc;
  GabbleTubesChannel *tube;

  /* Current active call */
  GabbleCallMucChannel *call;
  /* All calls, active one + potential ended ones */
  GList *calls;

  /* List of GSimpleAsyncResults for the various request for a call */
  GList *call_requests;
  gboolean call_initiating;

  GCancellable *requests_cancellable;

  GPtrArray *initial_channels;
  GArray *initial_handles;
  char **initial_ids;
};

static void
gabble_muc_channel_init (GabbleMucChannel *self)
{
  GabbleMucChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_MUC_CHANNEL, GabbleMucChannelPrivate);

  self->priv = priv;

  priv->requests_cancellable = g_cancellable_new ();
}

static TpHandle create_room_identity (GabbleMucChannel *)
  G_GNUC_WARN_UNUSED_RESULT;

#define NUM_SUPPORTED_MESSAGE_TYPES 3

/*  signatures for presence handlers */

static void handle_fill_presence (WockyMuc *muc,
    WockyStanza *stanza,
    gpointer user_data);

static void handle_renamed (GObject *source,
    WockyStanza *stanza,
    GHashTable *code,
    gpointer data);

static void handle_error (GObject *source,
    WockyStanza *stanza,
    WockyXmppError errnum,
    const gchar *message,
    gpointer data);

static void handle_join (WockyMuc *muc,
    WockyStanza *stanza,
    GHashTable *code,
    gpointer data);

static void handle_parted (GObject *source,
    WockyStanza *stanza,
    GHashTable *code,
    const gchar *actor_jid,
    const gchar *why,
    const gchar *msg,
    gpointer data);

static void handle_left (GObject *source,
    WockyStanza *stanza,
    GHashTable *code,
    WockyMucMember *who,
    const gchar *actor_jid,
    const gchar *why,
    const gchar *msg,
    gpointer data);

static void handle_presence (GObject *source,
    WockyStanza *stanza,
    GHashTable *code,
    WockyMucMember *who,
    gpointer data);

static void handle_perms (GObject *source,
    WockyStanza *stanza,
    GHashTable *code,
    const gchar *actor,
    const gchar *why,
    gpointer data);

/* signatures for message handlers */
static void handle_message (GObject *source,
    WockyStanza *stanza,
    WockyMucMsgType type,
    const gchar *xmpp_id,
    time_t stamp,
    WockyMucMember *who,
    const gchar *text,
    const gchar *subject,
    WockyMucMsgState state,
    gpointer data);

static void handle_errmsg (GObject *source,
    WockyStanza *stanza,
    WockyMucMsgType type,
    const gchar *xmpp_id,
    time_t stamp,
    WockyMucMember *who,
    const gchar *text,
    WockyXmppError error,
    WockyXmppErrorType etype,
    gpointer data);

static void
gabble_muc_channel_constructed (GObject *obj)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (obj);
  GabbleMucChannelPrivate *priv = self->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  TpHandleRepoIface *room_handles, *contact_handles;
  TpHandle target, initiator, self_handle;
  TpChannelTextMessageType types[NUM_SUPPORTED_MESSAGE_TYPES] = {
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
  };
  const gchar * supported_content_types[] = {
      "text/plain",
      NULL
  };
  void (*chain_up) (GObject *) =
    ((GObjectClass *) gabble_muc_channel_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (obj);

  priv->tube = NULL;

  room_handles = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_ROOM);
  contact_handles = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  /* get, and sanity-check, the room's jid */
  target = tp_base_channel_get_target_handle (base);
  priv->jid = tp_handle_inspect (room_handles, target);
  g_assert (priv->jid != NULL && strchr (priv->jid, '/') == NULL);

  /* The factory should have given us an initiator */
  initiator = tp_base_channel_get_initiator (base);
  g_assert (initiator != 0);

  /* create our own identity in the room */
  self_handle = create_room_identity (self);
  /* this causes us to have one ref to the self handle which is unreffed
   * at the end of this function */

  /* initialize our own role and affiliation */
  priv->self_role = ROLE_NONE;
  priv->self_affil = AFFILIATION_NONE;

  /* initialise the wocky muc object */
  {
    GabbleConnection *conn = GABBLE_CONNECTION (base_conn);
    WockyPorter *porter = gabble_connection_get_porter (conn);
    const gchar *room_jid = tp_handle_inspect (contact_handles, self_handle);
    gchar *user_jid = gabble_connection_get_full_jid (conn);
    WockyMuc *wmuc = g_object_new (WOCKY_TYPE_MUC,
        "porter", porter,
        "jid", room_jid,  /* room@service.name/nick */
        "user", user_jid, /* user@doma.in/resource  */
        NULL);

    /* various presence handlers */
    g_signal_connect (wmuc, "nick-change", (GCallback) handle_renamed,  self);
    g_signal_connect (wmuc, "presence",    (GCallback) handle_presence, self);
    g_signal_connect (wmuc, "joined",      (GCallback) handle_join,     self);
    g_signal_connect (wmuc, "permissions", (GCallback) handle_perms,    self);
    g_signal_connect (wmuc, "parted",      (GCallback) handle_parted,   self);
    g_signal_connect (wmuc, "left",        (GCallback) handle_left,     self);
    g_signal_connect (wmuc, "error",       (GCallback) handle_error,    self);

    g_signal_connect (wmuc, "fill-presence", G_CALLBACK (handle_fill_presence),
        self);

    /* message handler(s) (just one needed so far) */
    g_signal_connect (wmuc, "message",      (GCallback) handle_message, self);
    g_signal_connect (wmuc, "message-error",(GCallback) handle_errmsg,  self);

    priv->wmuc = wmuc;

    g_free (user_jid);
    g_object_unref (porter);
  }

  /* register object on the bus */
  tp_base_channel_register (base);

  /* initialize group mixin */
  tp_group_mixin_init (obj,
      G_STRUCT_OFFSET (GabbleMucChannel, group),
      contact_handles, self_handle);

  /* set initial group flags */
  tp_group_mixin_change_flags (obj,
      TP_CHANNEL_GROUP_FLAG_PROPERTIES |
      TP_CHANNEL_GROUP_FLAG_CHANNEL_SPECIFIC_HANDLES |
      TP_CHANNEL_GROUP_FLAG_HANDLE_OWNERS_NOT_AVAILABLE |
      TP_CHANNEL_GROUP_FLAG_CAN_ADD,
      0);

  /* initialize properties mixin */
  tp_properties_mixin_init (obj, G_STRUCT_OFFSET (
        GabbleMucChannel, properties));

  /* initialize message mixin */
  tp_message_mixin_init (obj, G_STRUCT_OFFSET (GabbleMucChannel, message_mixin),
      base_conn);
  tp_message_mixin_implement_sending (obj, gabble_muc_channel_send,
      NUM_SUPPORTED_MESSAGE_TYPES, types, 0,
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_FAILURES |
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_SUCCESSES,
      supported_content_types);

  tp_group_mixin_add_handle_owner (obj, self_handle, base_conn->self_handle);

  if (priv->invited)
    {
      /* invited: add ourself to local pending and the inviter to members */
      TpIntSet *members = tp_intset_new_containing (initiator);
      TpIntSet *pending = tp_intset_new_containing (self_handle);

      tp_group_mixin_change_members (obj, priv->invitation_message,
          members, NULL, pending, NULL,
          initiator, TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);

      tp_intset_destroy (members);
      tp_intset_destroy (pending);

      /* we've dealt with it (and copied it elsewhere), so there's no point
       * in keeping it */
      g_free (priv->invitation_message);
      priv->invitation_message = NULL;

      /* mark channel ready so NewChannel is emitted immediately */
      priv->ready = TRUE;
    }
  else
    {
      /* not invited: add ourselves to members (and hence join immediately) */
      GError *error = NULL;
      GArray *members = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);

      g_assert (initiator == base_conn->self_handle);
      g_assert (priv->invitation_message == NULL);

      g_array_append_val (members, self_handle);
      tp_group_mixin_add_members (obj, members, "", &error);
      g_assert (error == NULL);
      g_array_free (members, TRUE);
    }

  tp_handle_unref (contact_handles, self_handle);
}

static void
properties_disco_cb (GabbleDisco *disco,
                     GabbleDiscoRequest *request,
                     const gchar *jid,
                     const gchar *node,
                     LmMessageNode *query_result,
                     GError *error,
                     gpointer user_data)
{
  GabbleMucChannel *chan = user_data;
  TpIntSet *changed_props_val, *changed_props_flags;
  LmMessageNode *lm_node;
  const gchar *str;
  GValue val = { 0, };
  NodeIter i;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  if (error)
    {
      DEBUG ("got error %s", error->message);
      return;
    }

  changed_props_val = tp_intset_sized_new (NUM_ROOM_PROPS);
  changed_props_flags = tp_intset_sized_new (NUM_ROOM_PROPS);

  /*
   * Update room definition.
   */

  /* ROOM_PROP_NAME */
  lm_node = lm_message_node_get_child (query_result, "identity");
  if (lm_node)
    {
      const gchar *category, *type, *name;

      category = lm_message_node_get_attribute (lm_node, "category");
      type = lm_message_node_get_attribute (lm_node, "type");
      name = lm_message_node_get_attribute (lm_node, "name");

      if (!tp_strdiff (category, "conference") &&
          !tp_strdiff (type, "text") &&
          name != NULL)
        {
          g_value_init (&val, G_TYPE_STRING);
          g_value_set_string (&val, name);

          tp_properties_mixin_change_value (G_OBJECT (chan), ROOM_PROP_NAME,
                                                &val, changed_props_val);

          tp_properties_mixin_change_flags (G_OBJECT (chan), ROOM_PROP_NAME,
                                                TP_PROPERTY_FLAG_READ,
                                                0, changed_props_flags);

          g_value_unset (&val);
        }
    }

  for (i = node_iter (query_result); i; i = node_iter_next (i))
    {
      guint prop_id = INVALID_ROOM_PROP;
      LmMessageNode *child = node_iter_data (i);

      if (strcmp (child->name, "feature") == 0)
        {
          str = lm_message_node_get_attribute (child, "var");
          if (str == NULL)
            continue;

          /* ROOM_PROP_ANONYMOUS */
          if (strcmp (str, "muc_nonanonymous") == 0)
            {
              prop_id = ROOM_PROP_ANONYMOUS;
              g_value_init (&val, G_TYPE_BOOLEAN);
              g_value_set_boolean (&val, FALSE);
            }
          else if (strcmp (str, "muc_semianonymous") == 0 ||
                   strcmp (str, "muc_anonymous") == 0)
            {
              prop_id = ROOM_PROP_ANONYMOUS;
              g_value_init (&val, G_TYPE_BOOLEAN);
              g_value_set_boolean (&val, TRUE);
            }

          /* ROOM_PROP_INVITE_ONLY */
          else if (strcmp (str, "muc_open") == 0)
            {
              prop_id = ROOM_PROP_INVITE_ONLY;
              g_value_init (&val, G_TYPE_BOOLEAN);
              g_value_set_boolean (&val, FALSE);
            }
          else if (strcmp (str, "muc_membersonly") == 0)
            {
              prop_id = ROOM_PROP_INVITE_ONLY;
              g_value_init (&val, G_TYPE_BOOLEAN);
              g_value_set_boolean (&val, TRUE);
            }

          /* ROOM_PROP_MODERATED */
          else if (strcmp (str, "muc_unmoderated") == 0)
            {
              prop_id = ROOM_PROP_MODERATED;
              g_value_init (&val, G_TYPE_BOOLEAN);
              g_value_set_boolean (&val, FALSE);
            }
          else if (strcmp (str, "muc_moderated") == 0)
            {
              prop_id = ROOM_PROP_MODERATED;
              g_value_init (&val, G_TYPE_BOOLEAN);
              g_value_set_boolean (&val, TRUE);
            }

          /* ROOM_PROP_PASSWORD_REQUIRED */
          else if (strcmp (str, "muc_unsecure") == 0 ||
                   strcmp (str, "muc_unsecured") == 0)
            {
              prop_id = ROOM_PROP_PASSWORD_REQUIRED;
              g_value_init (&val, G_TYPE_BOOLEAN);
              g_value_set_boolean (&val, FALSE);
            }
          else if (strcmp (str, "muc_passwordprotected") == 0)
            {
              prop_id = ROOM_PROP_PASSWORD_REQUIRED;
              g_value_init (&val, G_TYPE_BOOLEAN);
              g_value_set_boolean (&val, TRUE);
            }

          /* ROOM_PROP_PERSISTENT */
          else if (strcmp (str, "muc_temporary") == 0)
            {
              prop_id = ROOM_PROP_PERSISTENT;
              g_value_init (&val, G_TYPE_BOOLEAN);
              g_value_set_boolean (&val, FALSE);
            }
          else if (strcmp (str, "muc_persistent") == 0)
            {
              prop_id = ROOM_PROP_PERSISTENT;
              g_value_init (&val, G_TYPE_BOOLEAN);
              g_value_set_boolean (&val, TRUE);
            }

          /* ROOM_PROP_PRIVATE */
          else if (strcmp (str, "muc_public") == 0)
            {
              prop_id = ROOM_PROP_PRIVATE;
              g_value_init (&val, G_TYPE_BOOLEAN);
              g_value_set_boolean (&val, FALSE);
            }
          else if (strcmp (str, "muc_hidden") == 0)
            {
              prop_id = ROOM_PROP_PRIVATE;
              g_value_init (&val, G_TYPE_BOOLEAN);
              g_value_set_boolean (&val, TRUE);
            }

          /* Ignored */
          else if (strcmp (str, NS_MUC) == 0)
            {
            }

          /* Unhandled */
          else
            {
              DEBUG ("unhandled feature '%s'", str);
            }
        }
      else if (strcmp (child->name, "x") == 0)
        {
          if (lm_message_node_has_namespace (child, NS_X_DATA, NULL))
            {
              NodeIter j;

              for (j = node_iter (child); j; j = node_iter_next (j))
                {
                  LmMessageNode *field = node_iter_data (j);
                  LmMessageNode *value_node;

                  if (strcmp (field->name, "field") != 0)
                    continue;

                  str = lm_message_node_get_attribute (field, "var");
                  if (str == NULL)
                    continue;

                  if (strcmp (str, "muc#roominfo_description") != 0)
                    continue;

                  value_node = lm_message_node_get_child (field, "value");
                  if (value_node == NULL)
                    continue;

                  str = lm_message_node_get_value (value_node);
                  if (str == NULL)
                    {
                      str = "";
                    }

                  prop_id = ROOM_PROP_DESCRIPTION;
                  g_value_init (&val, G_TYPE_STRING);
                  g_value_set_string (&val, str);
                }
            }
        }

      if (prop_id != INVALID_ROOM_PROP)
        {
          tp_properties_mixin_change_value (G_OBJECT (chan), prop_id, &val,
                                                changed_props_val);

          tp_properties_mixin_change_flags (G_OBJECT (chan), prop_id,
                                                TP_PROPERTY_FLAG_READ,
                                                0, changed_props_flags);

          g_value_unset (&val);
        }
    }

  /*
   * Emit signals.
   */
  tp_properties_mixin_emit_changed (G_OBJECT (chan), changed_props_val);
  tp_properties_mixin_emit_flags (G_OBJECT (chan), changed_props_flags);
  tp_intset_destroy (changed_props_val);
  tp_intset_destroy (changed_props_flags);
}

static void
room_properties_update (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv;
  TpBaseChannel *base;
  GabbleConnection *conn;
  GError *error = NULL;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));
  priv = chan->priv;
  base = TP_BASE_CHANNEL (chan);
  conn = GABBLE_CONNECTION (tp_base_channel_get_connection (base));

  if (gabble_disco_request (conn->disco, GABBLE_DISCO_TYPE_INFO,
        priv->jid, NULL, properties_disco_cb, chan, G_OBJECT (chan),
        &error) == NULL)
    {
      DEBUG ("disco query failed: '%s'", error->message);
      g_error_free (error);
    }
}

static TpHandle
create_room_identity (GabbleMucChannel *chan)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (chan);
  GabbleMucChannelPrivate *priv = chan->priv;
  TpBaseConnection *conn = tp_base_channel_get_connection (base);
  TpHandleRepoIface *contact_repo;
  gchar *alias = NULL;
  GabbleConnectionAliasSource source;

  contact_repo = tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT);

  g_assert (priv->self_jid == NULL);

  source = _gabble_connection_get_cached_alias (GABBLE_CONNECTION (conn),
      conn->self_handle, &alias);
  g_assert (alias != NULL);

  if (source == GABBLE_CONNECTION_ALIAS_FROM_JID)
    {
      /* If our 'alias' is, in fact, our JID, we'll just use the local part as
       * our MUC resource.
       */
      gchar *local_part;

      g_assert (gabble_decode_jid (alias, &local_part, NULL, NULL));
      g_assert (local_part != NULL);
      g_free (alias);

      alias = local_part;
    }

  priv->self_jid = g_string_new (priv->jid);
  g_string_append_c (priv->self_jid, '/');
  g_string_append (priv->self_jid, alias);

  g_free (alias);

  return tp_handle_ensure (contact_repo, priv->self_jid->str,
      GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);
}

static gboolean
send_join_request (GabbleMucChannel *gmuc,
                   const gchar *password,
                   GError **error)
{
  GabbleMucChannelPrivate *priv = gmuc->priv;

  g_object_set (priv->wmuc, "password", password, NULL);
  wocky_muc_join (priv->wmuc, NULL);

  /* this used to be a meaningful success/failure return, but the two-stage  *
   * async op involved means we don't have any way of detecting failure here */
  return TRUE;
}

static gboolean
send_leave_message (GabbleMucChannel *gmuc,
                    const gchar *reason)
{
  GabbleMucChannelPrivate *priv = gmuc->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (gmuc);
  LmMessage *msg;
  GError *error = NULL;
  gboolean ret;

  /* build the message */
  msg = (LmMessage *) wocky_muc_create_presence (priv->wmuc,
      WOCKY_STANZA_SUB_TYPE_UNAVAILABLE, reason, NULL);

  g_signal_emit (gmuc, signals[PRE_PRESENCE], 0, msg);

  /* send it */
  ret = _gabble_connection_send (
      GABBLE_CONNECTION (tp_base_channel_get_connection (base)), msg, &error);

  if (!ret)
    {
      DEBUG ("_gabble_connection_send failed");
      g_error_free (error);
    }
  else
    {
      DEBUG ("leave message sent");
    }

  lm_message_unref (msg);

  return ret;
}

static void
gabble_muc_channel_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = chan->priv;

  switch (property_id) {
    case PROP_STATE:
      g_value_set_uint (value, priv->state);
      break;
    case PROP_SELF_JID:
      g_value_set_string (value, priv->self_jid->str);
      break;
    case PROP_TUBE:
      g_value_set_object (value, priv->tube);
      break;
    case PROP_WOCKY_MUC:
      g_value_set_object (value, priv->wmuc);
      break;
    case PROP_INVITATION_MESSAGE:
      g_value_set_string (value, "");
      break;
    case PROP_INITIAL_CHANNELS:
      g_value_set_boxed (value, priv->initial_channels);
      break;
    case PROP_INITIAL_INVITEE_HANDLES:
      g_value_set_boxed (value, priv->initial_handles);
      break;
    case PROP_INITIAL_INVITEE_IDS:
      g_value_set_boxed (value, priv->initial_ids);
      break;
    case PROP_ORIGINAL_CHANNELS:
      /* We don't have a useful value for this - we don't necessarily know
       * which chatroom member is which global handle, and the main purpose
       * of OriginalChannels is to be able to split off merged channels,
       * which we can't do anyway in XMPP. */
      g_value_take_boxed (value, g_hash_table_new (NULL, NULL));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void channel_state_changed (GabbleMucChannel *chan,
                                   GabbleMucState prev_state,
                                   GabbleMucState new_state);

static void
gabble_muc_channel_set_property (GObject     *object,
                                 guint        property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = chan->priv;
  GabbleMucState prev_state;

  switch (property_id) {
    case PROP_STATE:
      prev_state = priv->state;
      priv->state = g_value_get_uint (value);

      if (priv->state != prev_state)
        channel_state_changed (chan, prev_state, priv->state);

      break;
    case PROP_INVITED:
      priv->invited = g_value_get_boolean (value);
      break;
    case PROP_INVITATION_MESSAGE:
      g_assert (priv->invitation_message == NULL);
      priv->invitation_message = g_value_dup_string (value);
      break;
    case PROP_INITIAL_CHANNELS:
      priv->initial_channels = g_value_dup_boxed (value);
      g_assert (priv->initial_channels != NULL);
      break;
    case PROP_INITIAL_INVITEE_HANDLES:
      priv->initial_handles = g_value_dup_boxed (value);
      g_assert (priv->initial_handles != NULL);
      break;
    case PROP_INITIAL_INVITEE_IDS:
      priv->initial_ids = g_value_dup_boxed (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_muc_channel_dispose (GObject *object);
static void gabble_muc_channel_finalize (GObject *object);
static gboolean gabble_muc_channel_add_member (GObject *obj, TpHandle handle,
    const gchar *message, GError **error);
static gboolean gabble_muc_channel_remove_member (GObject *obj,
    TpHandle handle, const gchar *message, GError **error);
static gboolean gabble_muc_channel_do_set_properties (GObject *obj,
    TpPropertiesContext *ctx, GError **error);

static void
gabble_muc_channel_fill_immutable_properties (
    TpBaseChannel *chan,
    GHashTable *properties)
{
  TP_BASE_CHANNEL_CLASS (gabble_muc_channel_parent_class)->fill_immutable_properties (
      chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
      TP_IFACE_CHANNEL_INTERFACE_CONFERENCE, "InitialChannels",
      TP_IFACE_CHANNEL_INTERFACE_CONFERENCE, "InitialInviteeHandles",
      TP_IFACE_CHANNEL_INTERFACE_CONFERENCE, "InitialInviteeIDs",
      TP_IFACE_CHANNEL_INTERFACE_CONFERENCE, "InvitationMessage",
      NULL);
}

static void
gabble_muc_channel_class_init (GabbleMucChannelClass *gabble_muc_channel_class)
{
  static TpDBusPropertiesMixinPropImpl conference_props[] = {
      { "Channels", "initial-channels", NULL, },
      { "InitialChannels", "initial-channels", NULL },
      { "InitialInviteeHandles", "initial-invitee-handles", NULL },
      { "InitialInviteeIDs", "initial-invitee-ids", NULL },
      { "InvitationMessage", "invitation-message", NULL },
      { "OriginalChannels", "original-channels", NULL },
      { NULL }
  };
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_muc_channel_class);
  TpBaseChannelClass *base_class = TP_BASE_CHANNEL_CLASS (object_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_muc_channel_class,
      sizeof (GabbleMucChannelPrivate));

  object_class->constructed = gabble_muc_channel_constructed;
  object_class->get_property = gabble_muc_channel_get_property;
  object_class->set_property = gabble_muc_channel_set_property;
  object_class->dispose = gabble_muc_channel_dispose;
  object_class->finalize = gabble_muc_channel_finalize;

  base_class->channel_type = TP_IFACE_CHANNEL_TYPE_TEXT;
  base_class->target_handle_type = TP_HANDLE_TYPE_ROOM;
  base_class->interfaces = gabble_muc_channel_interfaces;
  base_class->fill_immutable_properties = gabble_muc_channel_fill_immutable_properties;
  base_class->close = gabble_muc_channel_close;

  param_spec = g_param_spec_uint ("state", "Channel state",
      "The current state that the channel is in.",
      0, G_MAXUINT32, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  param_spec = g_param_spec_boolean ("invited", "Invited?",
      "Whether the user has been invited to the channel.", FALSE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INVITED, param_spec);

  param_spec = g_param_spec_string ("invitation-message",
      "Invitation message",
      "The message we were sent when invited; NULL if not invited or if "
      "already processed",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INVITATION_MESSAGE,
      param_spec);

  param_spec = g_param_spec_string ("self-jid", "Our self JID",
      "Our self muc jid in this room",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SELF_JID,
      param_spec);

  param_spec = g_param_spec_object ("tube", "Tube Channel",
      "The GabbleTubesChannel associated with this MUC (if any)",
      GABBLE_TYPE_TUBES_CHANNEL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TUBE, param_spec);

  param_spec = g_param_spec_object ("wocky-muc", "Wocky MUC Object",
      "The backend (Wocky) MUC instance",
      WOCKY_TYPE_MUC, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_WOCKY_MUC, param_spec);

  param_spec = g_param_spec_boxed ("initial-channels", "Initial Channels",
      "The initial channels offered with this Conference",
      TP_ARRAY_TYPE_OBJECT_PATH_LIST,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_CHANNELS,
      param_spec);

  param_spec = g_param_spec_boxed ("initial-invitee-handles",
      "Initial Invitee Handles",
      "The handles of the Conference's initial invitees",
      DBUS_TYPE_G_UINT_ARRAY,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_INVITEE_HANDLES,
      param_spec);

  param_spec = g_param_spec_boxed ("initial-invitee-ids",
      "Initial Invitee IDs",
      "The identifiers of the Conference's initial invitees",
      G_TYPE_STRV,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_INVITEE_IDS,
      param_spec);

  param_spec = g_param_spec_boxed ("original-channels", "OriginalChannels",
      "Map from channel-specific handles to originally-offered channels",
      TP_HASH_TYPE_CHANNEL_ORIGINATOR_MAP,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ORIGINAL_CHANNELS,
      param_spec);

  signals[READY] =
    g_signal_new ("ready",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[JOIN_ERROR] =
    g_signal_new ("join-error",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE, 1, G_TYPE_POINTER);

  signals[PRE_INVITE] =
    g_signal_new ("pre-invite",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__STRING,
                  G_TYPE_NONE, 1, G_TYPE_STRING);

  signals[CONTACT_JOIN] =
    g_signal_new ("contact-join",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[PRE_PRESENCE] =
    g_signal_new ("pre-presence",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE, 1, G_TYPE_POINTER);

  signals[NEW_TUBE] = g_signal_new ("new-tube",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, GABBLE_TYPE_TUBES_CHANNEL);

  signals[NEW_CALL] = g_signal_new ("new-call",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__OBJECT_POINTER,
                  G_TYPE_NONE, 2,
                  GABBLE_TYPE_CALL_MUC_CHANNEL,
                  G_TYPE_POINTER);

  tp_properties_mixin_class_init (object_class,
                                      G_STRUCT_OFFSET (GabbleMucChannelClass,
                                        properties_class),
                                      room_property_signatures, NUM_ROOM_PROPS,
                                      gabble_muc_channel_do_set_properties);


  tp_dbus_properties_mixin_implement_interface (object_class,
      TP_IFACE_QUARK_CHANNEL_INTERFACE_CONFERENCE,
      tp_dbus_properties_mixin_getter_gobject_properties, NULL,
      conference_props);

  tp_message_mixin_init_dbus_properties (object_class);

  tp_group_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleMucChannelClass, group_class),
      gabble_muc_channel_add_member,
      gabble_muc_channel_remove_member);
  tp_group_mixin_init_dbus_properties (object_class);
  tp_group_mixin_class_allow_self_removal (object_class);
}

static void clear_join_timer (GabbleMucChannel *chan);
static void clear_poll_timer (GabbleMucChannel *chan);

void
gabble_muc_channel_dispose (GObject *object)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("called");

  priv->dispose_has_run = TRUE;

  clear_join_timer (self);
  clear_poll_timer (self);

  tp_clear_object (&priv->wmuc);
  tp_clear_object (&priv->requests_cancellable);

  if (G_OBJECT_CLASS (gabble_muc_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_muc_channel_parent_class)->dispose (object);
}

void
gabble_muc_channel_finalize (GObject *object)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = self->priv;

  DEBUG ("called");

  /* free any data held directly by the object here */

  if (priv->self_jid)
    {
      g_string_free (priv->self_jid, TRUE);
    }

  g_free (priv->password);

  if (priv->initial_channels != NULL)
    {
      g_boxed_free (TP_ARRAY_TYPE_OBJECT_PATH_LIST, priv->initial_channels);
      priv->initial_channels = NULL;
    }

  if (priv->initial_handles != NULL)
    {
      g_boxed_free (DBUS_TYPE_G_UINT_ARRAY, priv->initial_handles);
      priv->initial_handles = NULL;
    }

  if (priv->initial_ids != NULL)
    {
      g_boxed_free (G_TYPE_STRV, priv->initial_ids);
      priv->initial_ids = NULL;
    }

  tp_properties_mixin_finalize (object);
  tp_group_mixin_finalize (object);
  tp_message_mixin_finalize (object);

  G_OBJECT_CLASS (gabble_muc_channel_parent_class)->finalize (object);
}

static void clear_join_timer (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv = chan->priv;

  if (priv->join_timer_id != 0)
    {
      g_source_remove (priv->join_timer_id);
      priv->join_timer_id = 0;
    }
}

static void clear_poll_timer (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv = chan->priv;

  if (priv->poll_timer_id != 0)
    {
      g_source_remove (priv->poll_timer_id);
      priv->poll_timer_id = 0;
    }
}

static void
change_password_flags (GabbleMucChannel *chan,
                       TpChannelPasswordFlags add,
                       TpChannelPasswordFlags del)
{
  GabbleMucChannelPrivate *priv;
  TpChannelPasswordFlags added, removed;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = chan->priv;

  added = add & ~priv->password_flags;
  priv->password_flags |= added;

  removed = del & priv->password_flags;
  priv->password_flags &= ~removed;

  if (add != 0 || del != 0)
    {
      DEBUG ("emitting password flags changed, added 0x%X, removed 0x%X",
              added, removed);

      tp_svc_channel_interface_password_emit_password_flags_changed (
          chan, added, removed);
    }
}

static void
provide_password_return_if_pending (GabbleMucChannel *chan, gboolean success)
{
  GabbleMucChannelPrivate *priv = chan->priv;

  if (priv->password_ctx)
    {
      dbus_g_method_return (priv->password_ctx, success);
      priv->password_ctx = NULL;
    }

  if (success)
    {
      change_password_flags (chan, 0, TP_CHANNEL_PASSWORD_FLAG_PROVIDE);
    }
}

static void close_channel (GabbleMucChannel *chan, const gchar *reason,
    gboolean inform_muc, TpHandle actor, guint reason_code);

static gboolean
timeout_join (gpointer data)
{
  GabbleMucChannel *chan = data;

  DEBUG ("join timed out, closing channel");

  provide_password_return_if_pending (chan, FALSE);

  close_channel (chan, NULL, FALSE, 0, 0);

  return FALSE;
}

static gboolean
timeout_poll (gpointer data)
{
  GabbleMucChannel *chan = data;

  DEBUG ("polling for room properties");

  room_properties_update (chan);

  return TRUE;
}

static void
channel_state_changed (GabbleMucChannel *chan,
                       GabbleMucState prev_state,
                       GabbleMucState new_state)
{
  GabbleMucChannelPrivate *priv = chan->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (chan);

  DEBUG ("state changed from %s to %s", muc_states[prev_state],
      muc_states[new_state]);

  if (new_state == MUC_STATE_INITIATED)
    {
      priv->join_timer_id =
        g_timeout_add_seconds (DEFAULT_JOIN_TIMEOUT, timeout_join, chan);
    }
  else if (new_state == MUC_STATE_JOINED)
    {
      gboolean low_bandwidth;
      gint interval;

      provide_password_return_if_pending (chan, TRUE);

      clear_join_timer (chan);

      g_object_get (GABBLE_CONNECTION (tp_base_channel_get_connection (base)), "low-bandwidth", &low_bandwidth, NULL);

      if (low_bandwidth)
        interval = PROPS_POLL_INTERVAL_LOW;
      else
        interval = PROPS_POLL_INTERVAL_HIGH;

      priv->poll_timer_id =
          g_timeout_add_seconds (interval, timeout_poll, chan);

      /* no need to keep this around any longer, if it's set */
      g_free (priv->password);
      priv->password = NULL;
    }
  else if (new_state == MUC_STATE_ENDED)
    {
      clear_poll_timer (chan);
    }

  if (new_state == MUC_STATE_JOINED || new_state == MUC_STATE_AUTH)
    {
      if (!priv->ready)
        {
          g_signal_emit (chan, signals[READY], 0);
          priv->ready = TRUE;
        }
    }
}


static void
close_channel (GabbleMucChannel *chan, const gchar *reason,
               gboolean inform_muc, TpHandle actor, guint reason_code)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (chan);
  GabbleMucChannelPrivate *priv = chan->priv;
  GabbleConnection *conn = GABBLE_CONNECTION (
      tp_base_channel_get_connection (base));

  TpIntSet *set;
  GArray *handles;
  GError error = { TP_ERRORS,
      TP_ERROR_CANCELLED,
      "Muc channel closed below us"
  };

  DEBUG ("Closing");

  if (tp_base_channel_is_destroyed (base))
    return;

  gabble_muc_channel_close_tube (chan);

  muc_call_channel_finish_requests (chan, NULL, &error);

  g_cancellable_cancel (priv->requests_cancellable);

  while (priv->calls != NULL)
    gabble_base_call_channel_close (
        GABBLE_BASE_CALL_CHANNEL (priv->calls->data));

  /* Remove us from member list */
  set = tp_intset_new ();
  tp_intset_add (set, TP_GROUP_MIXIN (chan)->self_handle);

  tp_group_mixin_change_members ((GObject *) chan,
                                     (reason != NULL) ? reason : "",
                                     NULL, set, NULL, NULL, actor,
                                     reason_code);

  tp_intset_destroy (set);

  /* Inform the MUC if requested */
  if (inform_muc && priv->state >= MUC_STATE_INITIATED)
    {
      send_leave_message (chan, reason);
    }

  handles = tp_handle_set_to_array (chan->group.members);

  gabble_presence_cache_update_many (conn->presence_cache, handles,
    NULL, GABBLE_PRESENCE_UNKNOWN, NULL, 0);

  g_array_free (handles, TRUE);

  /* Update state and emit Closed signal */
  g_object_set (chan, "state", MUC_STATE_ENDED, NULL);

  tp_base_channel_destroyed (base);
}

gboolean
_gabble_muc_channel_is_ready (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = chan->priv;

  return priv->ready;
}

static gboolean
handle_nick_conflict (GabbleMucChannel *chan,
                      GError **tp_error)
{
  GabbleMucChannelPrivate *priv = chan->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (chan);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (chan);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      tp_base_channel_get_connection (base), TP_HANDLE_TYPE_CONTACT);
  TpHandle self_handle;
  TpIntSet *add_rp, *remove_rp;

  if (priv->nick_retry_count >= MAX_NICK_RETRIES)
    {
      g_set_error (tp_error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "nickname already in use and retry count exceeded");
      return FALSE;
    }

  /* Add a _ to our jid, and update the group mixin's self handle
   * and remote pending members appropriately.
   */
  g_string_append_c (priv->self_jid, '_');
  g_object_set (priv->wmuc, "jid", priv->self_jid->str, NULL);
  self_handle = tp_handle_ensure (contact_repo, priv->self_jid->str,
      GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);

  add_rp = tp_intset_new ();
  remove_rp = tp_intset_new ();
  tp_intset_add (add_rp, self_handle);
  tp_intset_add (remove_rp, mixin->self_handle);

  tp_group_mixin_change_self_handle ((GObject *) chan, self_handle);
  tp_group_mixin_change_members ((GObject *) chan, NULL, NULL, remove_rp, NULL,
      add_rp, 0, TP_CHANNEL_GROUP_CHANGE_REASON_RENAMED);

  tp_intset_destroy (add_rp);
  tp_intset_destroy (remove_rp);
  tp_handle_unref (contact_repo, self_handle);

  priv->nick_retry_count++;
  return send_join_request (chan, priv->password, tp_error);
}

static LmHandlerResult
room_created_submit_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                              LmMessage *reply_msg, GObject *object,
                              gpointer user_data)
{
  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      DEBUG ("failed to submit room config");
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static LmMessageNode *
config_form_get_form_node (LmMessage *msg)
{
  LmMessageNode *node;
  NodeIter i;

  /* find the query node */
  node = lm_message_node_get_child (wocky_stanza_get_top_node (msg), "query");
  if (node == NULL)
    return NULL;

  /* then the form node */
  for (i = node_iter (node); i; i = node_iter_next (i))
    {
      LmMessageNode *child = node_iter_data (i);

      if (tp_strdiff (child->name, "x"))
        {
          continue;
        }

      if (!lm_message_node_has_namespace (child, NS_X_DATA, NULL))
        {
          continue;
        }

      if (tp_strdiff (lm_message_node_get_attribute (child, "type"), "form"))
        {
          continue;
        }

      return child;
    }

  return NULL;
}

static LmHandlerResult
perms_config_form_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                            LmMessage *reply_msg, GObject *object,
                            gpointer user_data)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = chan->priv;
  LmMessageNode *form_node;
  NodeIter i;

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      DEBUG ("request for config form denied, property permissions "
                 "will be inaccurate");
      goto OUT;
    }

  /* just in case our affiliation has changed in the meantime */
  if (priv->self_affil != AFFILIATION_OWNER)
    goto OUT;

  form_node = config_form_get_form_node (reply_msg);
  if (form_node == NULL)
    {
      DEBUG ("form node node found, property permissions will be inaccurate");
      goto OUT;
    }

  for (i = node_iter (form_node); i; i = node_iter_next (i))
    {
      const gchar *var;
      LmMessageNode *node = node_iter_data (i);

      if (strcmp (node->name, "field") != 0)
        continue;

      var = lm_message_node_get_attribute (node, "var");
      if (var == NULL)
        continue;

      if (strcmp (var, "muc#roomconfig_roomdesc") == 0 ||
          strcmp (var, "muc#owner_roomdesc") == 0)
        {
          if (tp_properties_mixin_is_readable (G_OBJECT (chan),
                                                   ROOM_PROP_DESCRIPTION))
            {
              tp_properties_mixin_change_flags (G_OBJECT (chan),
                  ROOM_PROP_DESCRIPTION, TP_PROPERTY_FLAG_WRITE, 0,
                  NULL);

              goto OUT;
            }
        }
    }

OUT:
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
update_permissions (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv = chan->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (chan);
  TpChannelGroupFlags grp_flags_add, grp_flags_rem;
  TpPropertyFlags prop_flags_add, prop_flags_rem;
  TpIntSet *changed_props_val, *changed_props_flags;

  /*
   * Update group flags.
   */
  grp_flags_add = TP_CHANNEL_GROUP_FLAG_CAN_ADD |
                  TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD;
  grp_flags_rem = 0;

  if (priv->self_role == ROLE_MODERATOR)
    {
      grp_flags_add |= TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
                       TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE;
    }
  else
    {
      grp_flags_rem |= TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
                       TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE;
    }

  tp_group_mixin_change_flags ((GObject *) chan, grp_flags_add, grp_flags_rem);


  /*
   * Update write capabilities based on room configuration
   * and own role and affiliation.
   */

  changed_props_val = tp_intset_sized_new (NUM_ROOM_PROPS);
  changed_props_flags = tp_intset_sized_new (NUM_ROOM_PROPS);

  /*
   * Subject
   *
   * FIXME: this might be allowed for participants/moderators only,
   *        so for now just rely on the server making that call.
   */

  if (priv->self_role >= ROLE_VISITOR)
    {
      prop_flags_add = TP_PROPERTY_FLAG_WRITE;
      prop_flags_rem = 0;
    }
  else
    {
      prop_flags_add = 0;
      prop_flags_rem = TP_PROPERTY_FLAG_WRITE;
    }

  tp_properties_mixin_change_flags (G_OBJECT (chan),
      ROOM_PROP_SUBJECT, prop_flags_add, prop_flags_rem,
      changed_props_flags);

  /* The room properties below are part of the "room definition", so are
   * defined by the XEP to be editable only by owners. */

  if (priv->self_affil == AFFILIATION_OWNER)
    {
      prop_flags_add = TP_PROPERTY_FLAG_WRITE;
      prop_flags_rem = 0;
    }
  else
    {
      prop_flags_add = 0;
      prop_flags_rem = TP_PROPERTY_FLAG_WRITE;
    }

  tp_properties_mixin_change_flags (G_OBJECT (chan),
      ROOM_PROP_ANONYMOUS, prop_flags_add, prop_flags_rem,
      changed_props_flags);

  tp_properties_mixin_change_flags (G_OBJECT (chan),
      ROOM_PROP_INVITE_ONLY, prop_flags_add, prop_flags_rem,
      changed_props_flags);

  tp_properties_mixin_change_flags (G_OBJECT (chan),
      ROOM_PROP_INVITE_RESTRICTED, prop_flags_add, prop_flags_rem,
      changed_props_flags);

  tp_properties_mixin_change_flags (G_OBJECT (chan),
      ROOM_PROP_MODERATED, prop_flags_add, prop_flags_rem,
      changed_props_flags);

  tp_properties_mixin_change_flags (G_OBJECT (chan),
      ROOM_PROP_NAME, prop_flags_add, prop_flags_rem,
      changed_props_flags);

  tp_properties_mixin_change_flags (G_OBJECT (chan),
      ROOM_PROP_PASSWORD, prop_flags_add, prop_flags_rem,
      changed_props_flags);

  tp_properties_mixin_change_flags (G_OBJECT (chan),
      ROOM_PROP_PASSWORD_REQUIRED, prop_flags_add, prop_flags_rem,
      changed_props_flags);

  tp_properties_mixin_change_flags (G_OBJECT (chan),
      ROOM_PROP_PERSISTENT, prop_flags_add, prop_flags_rem,
      changed_props_flags);

  tp_properties_mixin_change_flags (G_OBJECT (chan),
      ROOM_PROP_PRIVATE, prop_flags_add, prop_flags_rem,
      changed_props_flags);

  if (priv->self_affil == AFFILIATION_OWNER)
    {
      /* request the configuration form purely to see if the description
       * is writable by us in this room. sigh. GO MUC!!! */
      LmMessage *msg;
      LmMessageNode *node;
      GError *error = NULL;
      gboolean success;

      msg = lm_message_new_with_sub_type (priv->jid,
          LM_MESSAGE_TYPE_IQ, LM_MESSAGE_SUB_TYPE_GET);
      node = lm_message_node_add_child (
          wocky_stanza_get_top_node (msg), "query", NULL);
      lm_message_node_set_attribute (node, "xmlns", NS_MUC_OWNER);

      success = _gabble_connection_send_with_reply (
          GABBLE_CONNECTION (tp_base_channel_get_connection (base)), msg,
          perms_config_form_reply_cb, G_OBJECT (chan), NULL,
          &error);

      lm_message_unref (msg);

      if (!success)
        {
          DEBUG ("failed to request config form: %s", error->message);
          g_error_free (error);
        }
    }
  else
    {
      /* mark description unwritable if we're no longer an owner */
      tp_properties_mixin_change_flags (G_OBJECT (chan),
          ROOM_PROP_DESCRIPTION, 0, TP_PROPERTY_FLAG_WRITE,
          changed_props_flags);
    }

  /*
   * Emit signals.
   */
  tp_properties_mixin_emit_changed (G_OBJECT (chan), changed_props_val);
  tp_properties_mixin_emit_flags (G_OBJECT (chan), changed_props_flags);
  tp_intset_destroy (changed_props_val);
  tp_intset_destroy (changed_props_flags);
}



/* ************************************************************************* */
/* wocky MUC implementation */
static GabbleMucRole
get_role_from_backend (WockyMucRole role)
{
  switch (role)
    {
      case WOCKY_MUC_ROLE_NONE:
        return ROLE_NONE;
      case WOCKY_MUC_ROLE_VISITOR:
        return ROLE_VISITOR;
      case WOCKY_MUC_ROLE_PARTICIPANT:
        return ROLE_PARTICIPANT;
      case WOCKY_MUC_ROLE_MODERATOR:
        return ROLE_MODERATOR;
      default:
        DEBUG ("unknown role '%d' -- defaulting to ROLE_VISITOR", role);
        return ROLE_VISITOR;
    }
}

static GabbleMucAffiliation
get_aff_from_backend (WockyMucAffiliation aff)
{
  switch (aff)
    {
      case WOCKY_MUC_AFFILIATION_OUTCAST:
      case WOCKY_MUC_AFFILIATION_NONE:
        return AFFILIATION_NONE;
      case WOCKY_MUC_AFFILIATION_MEMBER:
        return AFFILIATION_MEMBER;
      case WOCKY_MUC_AFFILIATION_ADMIN:
        return AFFILIATION_ADMIN;
      case WOCKY_MUC_AFFILIATION_OWNER:
        return AFFILIATION_OWNER;
      default:
        DEBUG ("unknown affiliation %d -- defaulting to AFFILIATION_NONE", aff);
        return AFFILIATION_NONE;
    }
}

/* connect to wocky-muc:SIG_PRESENCE_ERROR */
static void
handle_error (GObject *source,
    WockyStanza *stanza,
    WockyXmppError errnum,
    const gchar *message,
    gpointer data)
{
  GabbleMucChannel *gmuc = GABBLE_MUC_CHANNEL (data);
  GabbleMucChannelPrivate *priv = gmuc->priv;
  TpChannelGroupChangeReason reason = TP_CHANNEL_GROUP_CHANGE_REASON_NONE;

  g_assert (GABBLE_IS_MUC_CHANNEL (gmuc));

  if (priv->state >= MUC_STATE_JOINED)
    {
      DEBUG ("presence error while already member of the channel -- NYI");
      return;
    }

  if (errnum == WOCKY_XMPP_ERROR_NOT_AUTHORIZED)
    {
      /* channel can sit requiring a password indefinitely */
      clear_join_timer (gmuc);

      /* Password already provided and incorrect? */
      if (priv->state == MUC_STATE_AUTH)
        {
          provide_password_return_if_pending (gmuc, FALSE);
          return;
        }

      DEBUG ("password required to join, changing password flags");
      change_password_flags (gmuc, TP_CHANNEL_PASSWORD_FLAG_PROVIDE, 0);
      g_object_set (gmuc, "state", MUC_STATE_AUTH, NULL);
    }
  else
    {
      GError *tp_error /* doesn't need initializing */;

      switch (errnum)
        {
          case WOCKY_XMPP_ERROR_FORBIDDEN:
            tp_error = g_error_new (TP_ERRORS, TP_ERROR_CHANNEL_BANNED,
                "banned from room");
            reason = TP_CHANNEL_GROUP_CHANGE_REASON_BANNED;
            break;
          case WOCKY_XMPP_ERROR_SERVICE_UNAVAILABLE:
            tp_error = g_error_new (TP_ERRORS, TP_ERROR_CHANNEL_FULL,
                "room is full");
            reason = TP_CHANNEL_GROUP_CHANGE_REASON_BUSY;
            break;

          case WOCKY_XMPP_ERROR_REGISTRATION_REQUIRED:
            tp_error = g_error_new (TP_ERRORS, TP_ERROR_CHANNEL_INVITE_ONLY,
                "room is invite only");
            break;

          case WOCKY_XMPP_ERROR_CONFLICT:
            if (handle_nick_conflict (gmuc, &tp_error))
              return;
            break;

          default:
            tp_error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                "%s", wocky_xmpp_error_description (errnum));
            break;
        }

      g_signal_emit (gmuc, signals[JOIN_ERROR], 0, tp_error);

      close_channel (gmuc, tp_error->message, FALSE, 0, reason);
      g_error_free (tp_error);
    }
}

static void
tube_closed_cb (GabbleTubesChannel *chan, gpointer user_data)
{
  GabbleMucChannel *gmuc = GABBLE_MUC_CHANNEL (user_data);
  GabbleMucChannelPrivate *priv = gmuc->priv;
  TpHandle room;

  if (priv->tube != NULL)
    {
      priv->tube = NULL;
      g_object_get (chan, "handle", &room, NULL);
      DEBUG ("removing MUC tubes channel with handle %d", room);
      g_object_unref (chan);
    }
}

static GabbleTubesChannel *
new_tube (GabbleMucChannel *gmuc,
    TpHandle initiator,
    gboolean requested)
{
  GabbleMucChannelPrivate *priv = gmuc->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (gmuc);
  TpBaseConnection *conn = tp_base_channel_get_connection (base);
  char *object_path;

  g_assert (priv->tube == NULL);

  object_path = g_strdup_printf ("%s/MucTubesChannel%u",
      conn->object_path, tp_base_channel_get_target_handle (base));

  DEBUG ("creating new tubes chan, object path %s", object_path);

  priv->tube = g_object_new (GABBLE_TYPE_TUBES_CHANNEL,
      "connection", tp_base_channel_get_connection (base),
      "object-path", object_path,
      "handle", tp_base_channel_get_target_handle (base),
      "handle-type", TP_HANDLE_TYPE_ROOM,
      "muc", gmuc,
      "initiator-handle", initiator,
      "requested", requested,
      NULL);

  g_signal_connect (priv->tube, "closed", (GCallback) tube_closed_cb, gmuc);

  g_signal_emit (gmuc, signals[NEW_TUBE], 0 , priv->tube);

  g_free (object_path);

  return priv->tube;
}

/* ************************************************************************* */
/* presence related signal handlers                                          */

/* not actually a signal handler, but used by them:                        *
 * creates a tube if none exists, and then prods the presence handler      *
 * in the gabble tubes implementation to do whatever else needs to be done */
static void
handle_tube_presence (GabbleMucChannel *gmuc,
    TpHandle from,
    WockyStanza *stanza)
{
  GabbleMucChannelPrivate *priv = gmuc->priv;
  WockyNode *node = wocky_stanza_get_top_node (stanza);

  if (from == 0)
    return;

  if (priv->tube == NULL)
    {
      WockyNode *tubes;
      tubes = wocky_node_get_child_ns (node, "tubes", NS_TUBES);

      /* presence doesn't contain tubes information, no need
       * to create a tubes channel */
      if (tubes == NULL)
        return;

      /* MUC Tubes channels (as opposed to the individual tubes) don't
       * have a well-defined initiator (they're a consensus) so use 0 */
      priv->tube = new_tube (gmuc, 0, FALSE);
    }

  gabble_tubes_channel_presence_updated (priv->tube, from, node);
}


/* connect to wocky-muc:SIG_PARTED, which we will receive when the MUC tells *
 * us that we have left the channel                                          */
static void
handle_parted (GObject *source,
    WockyStanza *stanza,
    GHashTable *code,
    const gchar *actor_jid,
    const gchar *why,
    const gchar *msg,
    gpointer data)
{
  WockyMuc *wmuc = WOCKY_MUC (source);
  GabbleMucChannel *gmuc = GABBLE_MUC_CHANNEL (data);
  TpBaseChannel *base = TP_BASE_CHANNEL (gmuc);
  GabbleMucChannelPrivate *priv = gmuc->priv;
  TpChannelGroupChangeReason reason = TP_CHANNEL_GROUP_CHANGE_REASON_NONE;
  TpHandleRepoIface *contact_repo =
    tp_base_connection_get_handles (tp_base_channel_get_connection (base),
        TP_HANDLE_TYPE_CONTACT);
  TpIntSet *handles = NULL;
  TpHandle member = 0;
  TpHandle actor = 0;
  int x = 0;
  static const gpointer banned = GUINT_TO_POINTER (WOCKY_MUC_CODE_BANNED);
  static const gpointer const kicked[] =
    { GUINT_TO_POINTER (WOCKY_MUC_CODE_KICKED),
      GUINT_TO_POINTER (WOCKY_MUC_CODE_KICKED_AFFILIATION),
      GUINT_TO_POINTER (WOCKY_MUC_CODE_KICKED_ROOM_PRIVATISED),
      GUINT_TO_POINTER (WOCKY_MUC_CODE_KICKED_SHUTDOWN),
      NULL };
  const char *jid = wocky_muc_jid (wmuc);

  member = tp_handle_ensure (contact_repo, jid, NULL, NULL);

  if (member == 0)
    {
      DEBUG ("bizarre: ignoring our own malformed MUC JID '%s'", jid);
      return;
    }

  handles = tp_intset_new ();
  tp_intset_add (handles, member);

  if (actor_jid != NULL)
    {
      actor = tp_handle_ensure (contact_repo, actor_jid, NULL, NULL);
      if (actor == 0)
        DEBUG ("ignoring invalid actor JID %s", actor_jid);
    }

  if (g_hash_table_lookup (code, banned) != NULL)
    reason = TP_CHANNEL_GROUP_CHANGE_REASON_BANNED;
  else
    for (x = 0; kicked[x] != NULL; x++)
      {
        if (g_hash_table_lookup (code, kicked[x]) != NULL)
          reason = TP_CHANNEL_GROUP_CHANGE_REASON_KICKED;
      }

  /* handle_tube_presence creates tubes if need be, so bypass it here: */
  if (priv->tube != NULL)
    gabble_tubes_channel_presence_updated (priv->tube, member,
      wocky_stanza_get_top_node (stanza));

  close_channel (gmuc, why, FALSE, actor, reason);

  if (actor != 0)
    tp_handle_unref (contact_repo, actor);
  tp_intset_destroy (handles);
  tp_handle_unref (contact_repo, member);
}


/* connect to wocky-muc:SIG_LEFT, which we will receive when the MUC informs *
 * us someone [else] has left the channel                                    */
static void
handle_left (GObject *source,
    WockyStanza *stanza,
    GHashTable *code,
    WockyMucMember *who,
    const gchar *actor_jid,
    const gchar *why,
    const gchar *msg,
    gpointer data)
{
  GabbleMucChannel *gmuc = GABBLE_MUC_CHANNEL (data);
  TpBaseChannel *base = TP_BASE_CHANNEL (gmuc);
  GabbleMucChannelPrivate *priv = gmuc->priv;
  TpChannelGroupChangeReason reason = TP_CHANNEL_GROUP_CHANGE_REASON_NONE;
  TpHandleRepoIface *contact_repo =
    tp_base_connection_get_handles (tp_base_channel_get_connection (base),
        TP_HANDLE_TYPE_CONTACT);
  TpIntSet *handles = NULL;
  TpHandle member = 0;
  TpHandle actor = 0;
  int x = 0;
  static const gpointer banned = GUINT_TO_POINTER (WOCKY_MUC_CODE_BANNED);
  static const gpointer const kicked[] =
    { GUINT_TO_POINTER (WOCKY_MUC_CODE_KICKED),
      GUINT_TO_POINTER (WOCKY_MUC_CODE_KICKED_AFFILIATION),
      GUINT_TO_POINTER (WOCKY_MUC_CODE_KICKED_ROOM_PRIVATISED),
      GUINT_TO_POINTER (WOCKY_MUC_CODE_KICKED_SHUTDOWN),
      NULL };

  member = tp_handle_ensure (contact_repo, who->from, NULL, NULL);

  if (member == 0)
    {
      DEBUG ("ignoring malformed MUC JID '%s'", who->from);
      return;
    }

  handles = tp_intset_new ();
  tp_intset_add (handles, member);

  if (actor_jid != NULL)
    {
      actor = tp_handle_ensure (contact_repo, actor_jid, NULL, NULL);
      if (actor == 0)
        DEBUG ("ignoring invalid actor JID %s", actor_jid);
    }

  if (g_hash_table_lookup (code, banned) != NULL)
    reason = TP_CHANNEL_GROUP_CHANGE_REASON_BANNED;
  else
    for (x = 0; kicked[x] != NULL; x++)
      {
        if (g_hash_table_lookup (code, kicked[x]) != NULL)
          reason = TP_CHANNEL_GROUP_CHANGE_REASON_KICKED;
      }

  /* handle_tube_presence creates tubes if need be, so bypass it here: */
  if (priv->tube != NULL)
    gabble_tubes_channel_presence_updated (priv->tube, member,
        wocky_stanza_get_top_node (stanza));

  tp_group_mixin_change_members (data, why, NULL, handles, NULL, NULL,
      actor, reason);

  if (actor != 0)
    tp_handle_unref (contact_repo, actor);
  tp_intset_destroy (handles);
  tp_handle_unref (contact_repo, member);
}

/* connect to wocky-muc:SIG_PERM_CHANGE, which we will receive when the *
 * MUC informs us our role/affiliation has been altered                 */
static void
handle_perms (GObject *source,
    WockyStanza *stanza,
    GHashTable *code,
    const gchar *actor,
    const gchar *why,
    gpointer data)
{
  WockyMuc *wmuc = WOCKY_MUC (source);
  GabbleMucChannel *gmuc = GABBLE_MUC_CHANNEL (data);
  GabbleMucChannelPrivate *priv = gmuc->priv;
  TpHandle myself = TP_GROUP_MIXIN (gmuc)->self_handle;

  priv->self_role = get_role_from_backend (wocky_muc_role (wmuc));
  priv->self_affil = get_aff_from_backend (wocky_muc_affiliation (wmuc));

  room_properties_update (gmuc);
  update_permissions (gmuc);

  handle_tube_presence (gmuc, myself, stanza);
}

static void
handle_fill_presence (WockyMuc *muc,
    WockyStanza *stanza,
    gpointer user_data)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (user_data);
  GabbleMucChannelPrivate *priv = self->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  GabbleConnection *conn = GABBLE_CONNECTION (base_conn);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);
  TpHandle self_handle;

  self_handle = tp_handle_ensure (contact_repo, priv->self_jid->str,
      GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);
  gabble_presence_add_status_and_vcard (conn->self_presence, stanza);

  /* If we are invisible, show us as dnd in muc, since we can't be invisible */
  if (conn->self_presence->status == GABBLE_PRESENCE_HIDDEN)
    wocky_node_add_child_with_content (wocky_stanza_get_top_node (stanza),
        "show", JABBER_PRESENCE_SHOW_DND);

  /* Sync the presence we send over the wire with what is in our presence cache
   */
  gabble_presence_cache_update (conn->presence_cache, self_handle,
      NULL,
      conn->self_presence->status,
      conn->self_presence->status_message,
      0);

  g_signal_emit (self, signals[PRE_PRESENCE], 0, (LmMessage *) stanza);
}

/* connect to wocky-muc:SIG_NICK_CHANGE, which we will receive when the *
 * MUC informs us our nick has been changed for some reason             */
static void
handle_renamed (GObject *source,
    WockyStanza *stanza,
    GHashTable *code,
    gpointer data)
{
  WockyMuc *wmuc = WOCKY_MUC (source);
  GabbleMucChannel *gmuc = GABBLE_MUC_CHANNEL (data);
  TpBaseChannel *base = TP_BASE_CHANNEL (gmuc);
  TpHandleRepoIface *contact_repo =
    tp_base_connection_get_handles (tp_base_channel_get_connection (base),
        TP_HANDLE_TYPE_CONTACT);
  TpIntSet *old_self = tp_intset_new ();
  const gchar *me = wocky_muc_jid (wmuc);
  const gchar *me2 = wocky_muc_user (wmuc);
  TpHandle myself = tp_handle_ensure (contact_repo, me,
      GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);
  TpHandle userid = tp_handle_ensure (contact_repo, me2,
      GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);

  tp_intset_add (old_self, TP_GROUP_MIXIN (gmuc)->self_handle);
  tp_group_mixin_change_self_handle (data, myself);
  tp_group_mixin_add_handle_owner (data, myself, userid);
  tp_group_mixin_change_members (data, "", NULL, old_self, NULL, NULL, 0, 0);

  handle_tube_presence (gmuc, myself, stanza);

  tp_intset_destroy (old_self);
  tp_handle_unref (contact_repo, userid);
  tp_handle_unref (contact_repo, myself);
}

static void
update_roster_presence (GabbleMucChannel *gmuc,
    WockyMucMember *member,
    TpHandleRepoIface *contact_repo,
    TpHandleSet *members,
    TpHandleSet *owners,
    GHashTable *omap)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (gmuc);
  GabbleConnection *conn =
      GABBLE_CONNECTION (tp_base_channel_get_connection (base));
  TpHandle owner = 0;
  TpHandle handle = tp_handle_ensure (contact_repo, member->from,
      GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);

  if (member->jid != NULL)
    {
      owner = tp_handle_ensure (contact_repo, member->jid,
          GUINT_TO_POINTER (GABBLE_JID_GLOBAL), NULL);
      if (owner == 0)
        DEBUG ("Invalid owner handle '%s', treating as no owner", member->jid);
      else
        tp_handle_set_add (owners, owner);
    }

  gabble_presence_parse_presence_message (conn->presence_cache,
      handle, member->from, (LmMessage *) member->presence_stanza);

  tp_handle_set_add (members, handle);
  g_hash_table_insert (omap,
      GUINT_TO_POINTER (handle),
      GUINT_TO_POINTER (owner));

  tp_handle_unref (contact_repo, handle);
  /* make a note of the fact that owner JIDs are visible to us    */
  /* notify whomever that an identifiable contact joined the MUC  */
  if (owner != 0)
    {
      tp_group_mixin_change_flags (G_OBJECT (gmuc), 0,
          TP_CHANNEL_GROUP_FLAG_HANDLE_OWNERS_NOT_AVAILABLE);
      g_signal_emit (gmuc, signals[CONTACT_JOIN], 0, owner);
      tp_handle_unref (contact_repo, owner);
    }

  handle_tube_presence (gmuc, handle, member->presence_stanza);
}

/* connect to wocky_muc SIG_JOINED which we should receive when we receive   *
 * the final (ie our own) presence in the roster: (note that if our nick was *
 * changed by the MUC we will already have received a SIG_NICK_CHANGE:       */
static void
handle_join (WockyMuc *muc,
    WockyStanza *stanza,
    GHashTable *code,
    gpointer data)
{
  GabbleMucChannel *gmuc = GABBLE_MUC_CHANNEL (data);
  TpBaseChannel *base = TP_BASE_CHANNEL (gmuc);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base_conn,
        TP_HANDLE_TYPE_CONTACT);
  TpHandleSet *members = tp_handle_set_new (contact_repo);
  TpHandleSet *owners = tp_handle_set_new (contact_repo);
  GHashTable *omap = g_hash_table_new (g_direct_hash, g_direct_equal);
  GHashTable *member_jids = wocky_muc_members (muc);
  const gchar *me = wocky_muc_jid (muc);
  TpHandle myself = tp_handle_ensure (contact_repo, me,
      GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);
  GHashTableIter iter;
  WockyMucMember *member;

  g_hash_table_iter_init (&iter, member_jids);

  while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&member))
    update_roster_presence (gmuc, member, contact_repo,
      members, owners, omap);

  g_hash_table_insert (omap,
      GUINT_TO_POINTER (myself),
      GUINT_TO_POINTER (base_conn->self_handle));

  tp_handle_set_add (members, myself);
  tp_group_mixin_add_handle_owners (G_OBJECT (gmuc), omap);
  tp_group_mixin_change_members (G_OBJECT (gmuc), "",
      tp_handle_set_peek (members), NULL, NULL, NULL, 0, 0);

  /* accept the config of the room if it was created for us: */
  if (g_hash_table_lookup (code, (gpointer) WOCKY_MUC_CODE_NEW_ROOM))
    {
      GError *error = NULL;
      gboolean sent = FALSE;
      WockyStanza *accept = wocky_stanza_build (
          WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
          NULL, NULL,
            '(', "query", ':', WOCKY_NS_MUC_OWN,
              '(', "x", ':', WOCKY_XMPP_NS_DATA,
                '@', "type", "submit",
              ')',
            ')',
          NULL);

      sent = _gabble_connection_send_with_reply (
          GABBLE_CONNECTION (base_conn), accept,
          room_created_submit_reply_cb, data, NULL, &error);

      g_object_unref (accept);

      if (!sent)
        {
          DEBUG ("failed to send submit message: %s", error->message);
          g_error_free (error);

          g_object_unref (accept);
          close_channel (gmuc, NULL, TRUE, 0,
              TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

          goto out;
        }
    }

  g_object_set (gmuc, "state", MUC_STATE_JOINED, NULL);

 out:
  tp_handle_unref (contact_repo, myself);
  tp_handle_set_destroy (members);
  tp_handle_set_destroy (owners);
  g_hash_table_unref (omap);
  g_hash_table_unref (member_jids);
}

/* connect to wocky-muc:SIG_PRESENCE, which is fired for presences that are *
 * NOT our own after the initial roster has been received:                  */
static void
handle_presence (GObject *source,
    WockyStanza *stanza,
    GHashTable *code,
    WockyMucMember *who,
    gpointer data)
{
  GabbleMucChannel *gmuc = GABBLE_MUC_CHANNEL (data);
  GabbleMucChannelPrivate *priv = gmuc->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (gmuc);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  GabbleConnection *conn = GABBLE_CONNECTION (base_conn);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle owner = 0;
  TpHandle handle = tp_handle_ensure (contact_repo, who->from,
      GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);
  TpHandleSet *handles = tp_handle_set_new (contact_repo);

  /* is the 'real' jid field of the presence set? If so, use it: */
  if (who->jid != NULL)
    {
      owner = tp_handle_ensure (contact_repo, who->jid,
          GUINT_TO_POINTER (GABBLE_JID_GLOBAL), NULL);
      if (owner == 0)
        {
          DEBUG ("Invalid owner handle '%s' ignored", who->jid);
        }
      else /* note that JIDs are known to us in this MUC */
        {
          tp_group_mixin_change_flags (G_OBJECT (data), 0,
              TP_CHANNEL_GROUP_FLAG_HANDLE_OWNERS_NOT_AVAILABLE);
        }
    }

  gabble_presence_parse_presence_message (conn->presence_cache,
    handle, who->from, (LmMessage *) who->presence_stanza);

  /* add the member in quesion */
  tp_handle_set_add (handles, handle);
  tp_group_mixin_change_members (data, "", tp_handle_set_peek (handles),
      NULL, NULL, NULL, 0, 0);

  /* record the owner (0 for no owner) */
  tp_group_mixin_add_handle_owner (data, handle, owner);

  handle_tube_presence (gmuc, handle, stanza);

  if (!priv->call_initiating && priv->call == NULL)
    {
      WockyNode *m;
      /* Check for muji nodes */
      m = wocky_node_get_child_ns (
          wocky_stanza_get_top_node (stanza), "muji", NS_MUJI);
      if (m != NULL &&
          wocky_node_get_child_ns (m, "content", NS_MUJI) != NULL)
        {
          DEBUG ("Detected a muji call in progress, starting a call channel!");
          gabble_muc_channel_start_call_creation (gmuc, NULL);
        }
    }

  /* zap the handle refs we created */
 out:
  tp_handle_unref (contact_repo, handle);
  if (owner != 0)
    tp_handle_unref (contact_repo, owner);
  tp_handle_set_destroy (handles);
}

/* ************************************************************************ */
/* message signal handlers */

static void
handle_message (GObject *source,
    WockyStanza *stanza,
    WockyMucMsgType type,
    const gchar *xmpp_id,
    time_t stamp,
    WockyMucMember *who,
    const gchar *text,
    const gchar *subject,
    WockyMucMsgState state,
    gpointer data)
{
  GabbleMucChannel *gmuc = GABBLE_MUC_CHANNEL (data);
  TpBaseChannel *base = TP_BASE_CHANNEL (gmuc);
  TpBaseConnection *conn = tp_base_channel_get_connection (base);
  gboolean from_member = (who != NULL);

  TpChannelTextMessageType msg_type;
  TpHandleRepoIface *repo;
  TpHandleType handle_type;
  TpHandle from;

  if (from_member)
    {
      handle_type = TP_HANDLE_TYPE_CONTACT;
      repo = tp_base_connection_get_handles (conn, handle_type);
      from = tp_handle_ensure (repo, who->from,
          GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);

      if (from == 0)
        {
          DEBUG ("Message from MUC member with no handle, discarding.");
          return;
        }
    }
  else /* directly from MUC itself */
    {
      handle_type = TP_HANDLE_TYPE_ROOM;
      repo = tp_base_connection_get_handles (conn, handle_type);
      from = tp_base_channel_get_target_handle (base);
      tp_handle_ref (repo, from);
    }

  switch (type)
    {
      case WOCKY_MUC_MSG_NORMAL:
        msg_type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
        break;
      case WOCKY_MUC_MSG_ACTION:
        msg_type = TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION;
        break;
      default:
        msg_type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE;
    }

  if (text != NULL)
    _gabble_muc_channel_receive (gmuc,
        msg_type, handle_type, from, stamp, xmpp_id, text, stanza,
        GABBLE_TEXT_CHANNEL_SEND_NO_ERROR, TP_DELIVERY_STATUS_DELIVERED);

  if (from_member && state != WOCKY_MUC_MSG_STATE_NONE)
    {
      gint tp_msg_state;
      switch (state)
        {
          case WOCKY_MUC_MSG_STATE_ACTIVE:
            tp_msg_state = TP_CHANNEL_CHAT_STATE_ACTIVE;
            break;
          case WOCKY_MUC_MSG_STATE_TYPING:
            tp_msg_state = TP_CHANNEL_CHAT_STATE_COMPOSING;
            break;
          case WOCKY_MUC_MSG_STATE_INACTIVE:
            tp_msg_state = TP_CHANNEL_CHAT_STATE_INACTIVE;
            break;
          case WOCKY_MUC_MSG_STATE_PAUSED:
            tp_msg_state = TP_CHANNEL_CHAT_STATE_PAUSED;
            break;
          default:
            tp_msg_state = TP_CHANNEL_CHAT_STATE_ACTIVE;
        }
      _gabble_muc_channel_state_receive (gmuc, tp_msg_state, from);
    }

  if (subject != NULL)
    _gabble_muc_channel_handle_subject (gmuc, msg_type, handle_type, from,
        stamp, subject, stanza);

  tp_handle_unref (repo, from);
}

static void
handle_errmsg (GObject *source,
    WockyStanza *stanza,
    WockyMucMsgType type,
    const gchar *xmpp_id,
    time_t stamp,
    WockyMucMember *who,
    const gchar *text,
    WockyXmppError error,
    WockyXmppErrorType etype,
    gpointer data)
{
  GabbleMucChannel *gmuc = GABBLE_MUC_CHANNEL (data);
  TpBaseChannel *base = TP_BASE_CHANNEL (gmuc);
  TpBaseConnection *conn = tp_base_channel_get_connection (base);
  gboolean from_member = (who != NULL);
  TpChannelTextSendError tp_err = TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN;
  TpDeliveryStatus ds = TP_DELIVERY_STATUS_DELIVERED;
  TpHandleRepoIface *repo = NULL;
  TpHandleType handle_type;
  TpHandle from = 0;

  if (from_member)
    {
      handle_type = TP_HANDLE_TYPE_CONTACT;
      repo = tp_base_connection_get_handles (conn, handle_type);
      from = tp_handle_ensure (repo, who->from,
          GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);

      if (from == 0)
        {
          DEBUG ("Message from MUC member with no handle, discarding.");
          return;
        }
    }
  else /* directly from MUC itself */
    {
      handle_type = TP_HANDLE_TYPE_ROOM;
      repo = tp_base_connection_get_handles (conn, handle_type);
      from = tp_base_channel_get_target_handle (base);
      tp_handle_ref (repo, from);
    }

  tp_err = gabble_tp_send_error_from_wocky_xmpp_error (error);

  if (etype == WOCKY_XMPP_ERROR_TYPE_WAIT)
    ds = TP_DELIVERY_STATUS_TEMPORARILY_FAILED;
  else
    ds = TP_DELIVERY_STATUS_PERMANENTLY_FAILED;

  if (text != NULL)
    _gabble_muc_channel_receive (gmuc, TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
        handle_type, from, stamp, xmpp_id, text, stanza, tp_err, ds);

  tp_handle_unref (repo, from);
}

/* ************************************************************************* */
/**
 * _gabble_muc_channel_handle_subject: handle room subject updates
 */
void
_gabble_muc_channel_handle_subject (GabbleMucChannel *chan,
                                    TpChannelTextMessageType msg_type,
                                    TpHandleType handle_type,
                                    TpHandle sender,
                                    time_t timestamp,
                                    const gchar *subject,
                                    LmMessage *msg)
{
  gboolean is_error;
  GabbleMucChannelPrivate *priv;
  TpIntSet *changed_values, *changed_flags;
  GValue val = { 0, };

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = chan->priv;

  is_error = lm_message_get_sub_type (msg) == LM_MESSAGE_SUB_TYPE_ERROR;

  if (priv->properties_ctx)
    {
      tp_properties_context_remove (priv->properties_ctx,
          ROOM_PROP_SUBJECT);
    }

  if (is_error)
    {
      LmMessageNode *node;
      const gchar *err_desc = NULL;

      node = lm_message_node_get_child (
          wocky_stanza_get_top_node (msg), "error");
      if (node)
        {
          GabbleXmppError xmpp_error = gabble_xmpp_error_from_node (node,
              NULL);
          err_desc = gabble_xmpp_error_description (xmpp_error);
        }

      if (priv->properties_ctx)
        {
          GError *error = NULL;

          error = g_error_new (TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
              "%s", (err_desc) ? err_desc : "failed to change subject");

          tp_properties_context_return (priv->properties_ctx, error);
          priv->properties_ctx = NULL;

          /* Get the properties into a consistent state. */
          room_properties_update (chan);
        }

      return;
    }

  DEBUG ("updating new property value for subject");

  changed_values = tp_intset_sized_new (NUM_ROOM_PROPS);
  changed_flags = tp_intset_sized_new (NUM_ROOM_PROPS);

  /* ROOM_PROP_SUBJECT */
  g_value_init (&val, G_TYPE_STRING);
  g_value_set_string (&val, subject);

  tp_properties_mixin_change_value (G_OBJECT (chan),
      ROOM_PROP_SUBJECT, &val, changed_values);

  tp_properties_mixin_change_flags (G_OBJECT (chan),
      ROOM_PROP_SUBJECT, TP_PROPERTY_FLAG_READ, 0,
      changed_flags);

  g_value_unset (&val);

  /* ROOM_PROP_SUBJECT_CONTACT */
  g_value_init (&val, G_TYPE_UINT);

  if (handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      g_value_set_uint (&val, sender);
    }
  else
    {
      g_value_set_uint (&val, 0);
    }

  tp_properties_mixin_change_value (G_OBJECT (chan),
      ROOM_PROP_SUBJECT_CONTACT, &val, changed_values);

  tp_properties_mixin_change_flags (G_OBJECT (chan),
      ROOM_PROP_SUBJECT_CONTACT, TP_PROPERTY_FLAG_READ, 0,
      changed_flags);

  g_value_unset (&val);

  /* ROOM_PROP_SUBJECT_TIMESTAMP */
  g_value_init (&val, G_TYPE_UINT);
  g_value_set_uint (&val, timestamp);

  tp_properties_mixin_change_value (G_OBJECT (chan),
      ROOM_PROP_SUBJECT_TIMESTAMP, &val, changed_values);

  tp_properties_mixin_change_flags (G_OBJECT (chan),
      ROOM_PROP_SUBJECT_TIMESTAMP, TP_PROPERTY_FLAG_READ, 0,
      changed_flags);

  g_value_unset (&val);

  /* Emit signals */
  tp_properties_mixin_emit_changed (G_OBJECT (chan), changed_values);
  tp_properties_mixin_emit_flags (G_OBJECT (chan), changed_flags);
  tp_intset_destroy (changed_values);
  tp_intset_destroy (changed_flags);

  if (priv->properties_ctx)
    {
      if (tp_properties_context_return_if_done (priv->properties_ctx))
        {
          priv->properties_ctx = NULL;
        }
    }
}

/**
 * _gabble_muc_channel_receive: receive MUC messages
 */
void
_gabble_muc_channel_receive (GabbleMucChannel *chan,
                             TpChannelTextMessageType msg_type,
                             TpHandleType sender_handle_type,
                             TpHandle sender,
                             time_t timestamp,
                             const gchar *id,
                             const gchar *text,
                             LmMessage *msg,
                             TpChannelTextSendError send_error,
                             TpDeliveryStatus error_status)
{
  GabbleMucChannelPrivate *priv;
  TpBaseChannel *base;
  TpBaseConnection *base_conn;
  TpMessage *message;
  TpHandle muc_self_handle;
  gboolean is_echo;
  gboolean is_error;
  gchar *tmp;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = chan->priv;
  base = TP_BASE_CHANNEL (chan);
  base_conn = tp_base_channel_get_connection (base);
  muc_self_handle = chan->group.self_handle;

  /* Is this an error report? */
  is_error = (send_error != GABBLE_TEXT_CHANNEL_SEND_NO_ERROR);

  if (is_error && sender == muc_self_handle)
    {
      /* So this is a <message from="ourself" type="error">.  I can only think
       * that this would happen if we send an error stanza and the MUC reflects
       * it back at us, so let's just ignore it.
       */
      STANZA_DEBUG (msg, "ignoring error stanza from ourself");

      return;
    }

  /* Is this an echo from the MUC of a message we just sent? */
  is_echo = ((sender == muc_self_handle) && (timestamp == 0));

  /* Having excluded the "error from ourself" case, is_error and is_echo are
   * mutually exclusive.
   */

  /* Ignore messages from the channel.  The only such messages I have seen in
   * practice have been on devel@conference.pidgin.im, which sends useful
   * messages like "foo has set the subject to: ..." and "This room is not
   * anonymous".
   */
  if (!is_echo && !is_error && sender_handle_type == TP_HANDLE_TYPE_ROOM)
    {
      STANZA_DEBUG (msg, "ignoring message from muc");

      return;
    }

  message = tp_message_new (base_conn, 2, 2);

  /* Header common to normal message and delivery-echo */
  if (msg_type != TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL)
    tp_message_set_uint32 (message, 0, "message-type", msg_type);

  if (timestamp != 0)
    tp_message_set_uint64 (message, 0, "message-sent", timestamp);

  /* Body */
  tp_message_set_string (message, 1, "content-type", "text/plain");
  tp_message_set_string (message, 1, "content", text);

  if (is_error || is_echo)
    {
      /* Error reports and echos of our own messages are represented as
       * delivery reports.
       */

      TpMessage *delivery_report = tp_message_new (base_conn, 1, 1);
      TpDeliveryStatus status =
          is_error ? error_status : TP_DELIVERY_STATUS_DELIVERED;

      tmp = gabble_generate_id ();
      tp_message_set_string (delivery_report, 0, "message-token", tmp);
      g_free (tmp);

      tp_message_set_uint32 (delivery_report, 0, "message-type",
          TP_CHANNEL_TEXT_MESSAGE_TYPE_DELIVERY_REPORT);
      tp_message_set_uint32 (delivery_report, 0, "delivery-status", status);

      if (id != NULL)
        tp_message_set_string (delivery_report, 0, "delivery-token", id);

      if (is_error)
        tp_message_set_uint32 (delivery_report, 0, "delivery-error",
            send_error);

      /* We do not set a message-sender on the report: the intended recipient
       * of the original message was the MUC, so the spec says we should omit
       * it.
       *
       * The sender of the echo, however, is ourself.  (Unless we get errors
       * for messages that we didn't send, which would be odd.)
       */
      tp_message_set_handle (message, 0, "message-sender",
          TP_HANDLE_TYPE_CONTACT, muc_self_handle);

      /* If we sent the message whose delivery has succeeded or failed, we
       * trust the id='' attribute. */
      if (id != NULL)
        tp_message_set_string (message, 0, "message-token", id);

      tp_message_take_message (delivery_report, 0, "delivery-echo",
          message);

      tp_message_mixin_take_received (G_OBJECT (chan), delivery_report);
    }
  else
    {
      /* Messages from the MUC itself should have no sender. */
      if (sender_handle_type == TP_HANDLE_TYPE_CONTACT)
        tp_message_set_handle (message, 0, "message-sender",
            TP_HANDLE_TYPE_CONTACT, sender);

      if (timestamp != 0)
        tp_message_set_boolean (message, 0, "scrollback", TRUE);

      /* We can't trust the id='' attribute set by the contact to be unique
       * enough to be a message-token, so let's generate one locally.
       */
      tmp = gabble_generate_id ();
      tp_message_set_string (message, 0, "message-token", tmp);
      g_free (tmp);

      tp_message_mixin_take_received (G_OBJECT (chan), message);
    }
}

/**
 * _gabble_muc_channel_state_receive
 *
 * Send the D-BUS signal ChatStateChanged
 * on org.freedesktop.Telepathy.Channel.Interface.ChatState
 */
void
_gabble_muc_channel_state_receive (GabbleMucChannel *chan,
                                   guint state,
                                   guint from_handle)
{
  g_assert (state < NUM_TP_CHANNEL_CHAT_STATES);
  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  tp_svc_channel_interface_chat_state_emit_chat_state_changed (chan,
      from_handle, state);
}

static void
gabble_muc_channel_close (TpBaseChannel *base)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (base);

  close_channel (self, NULL, TRUE, 0, 0);
}


/**
 * gabble_muc_channel_get_password_flags
 *
 * Implements D-Bus method GetPasswordFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 */
static void
gabble_muc_channel_get_password_flags (TpSvcChannelInterfacePassword *iface,
                                       DBusGMethodInvocation *context)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (iface);
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (self));

  priv = self->priv;

  tp_svc_channel_interface_password_return_from_get_password_flags (context,
      priv->password_flags);
}


/**
 * gabble_muc_channel_provide_password
 *
 * Implements D-Bus method ProvidePassword
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_muc_channel_provide_password (TpSvcChannelInterfacePassword *iface,
                                     const gchar *password,
                                     DBusGMethodInvocation *context)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (iface);
  GError *error = NULL;
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (self));

  priv = self->priv;

  if ((priv->password_flags & TP_CHANNEL_PASSWORD_FLAG_PROVIDE) == 0 ||
      priv->password_ctx != NULL)
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                           "password cannot be provided in the current state");
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return;
    }

  if (!send_join_request (self, password, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return;
    }

  priv->password_ctx = context;
}


/**
 * gabble_muc_channel_send
 *
 * Indirectly implements (via TpMessageMixin) D-Bus method Send on interface
 * org.freedesktop.Telepathy.Channel.Type.Text and D-Bus method SendMessage on
 * Channel.Interface.Messages
 */
static void
gabble_muc_channel_send (GObject *obj,
                         TpMessage *message,
                         TpMessageSendingFlags flags)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (obj);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  GabbleMucChannelPrivate *priv = self->priv;

  flags &= TP_MESSAGE_SENDING_FLAG_REPORT_DELIVERY;

  gabble_message_util_send_message (obj,
      GABBLE_CONNECTION (tp_base_channel_get_connection (base)),
      message, flags,
      LM_MESSAGE_SUB_TYPE_GROUPCHAT, TP_CHANNEL_CHAT_STATE_ACTIVE,
      priv->jid, FALSE /* send nick */);
}

gboolean
gabble_muc_channel_send_invite (GabbleMucChannel *self,
                                const gchar *jid,
                                const gchar *message,
                                gboolean continue_,
                                GError **error)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  GabbleMucChannelPrivate *priv = self->priv;
  LmMessage *msg;
  LmMessageNode *x_node, *invite_node;
  gboolean result;

  g_signal_emit (self, signals[PRE_INVITE], 0, jid);

  msg = lm_message_new (priv->jid, LM_MESSAGE_TYPE_MESSAGE);

  x_node = lm_message_node_add_child (
      wocky_stanza_get_top_node (msg), "x", NULL);
  lm_message_node_set_attribute (x_node, "xmlns", NS_MUC_USER);

  invite_node = lm_message_node_add_child (x_node, "invite", NULL);

  lm_message_node_set_attribute (invite_node, "to", jid);

  if (message != NULL && *message != '\0')
    {
      lm_message_node_add_child (invite_node, "reason", message);
    }

  if (continue_)
    {
      lm_message_node_add_child (invite_node, "continue", NULL);
    }

  DEBUG ("sending MUC invitation for room %s to contact %s with reason "
      "\"%s\"", priv->jid, jid, message);

  result = _gabble_connection_send (
      GABBLE_CONNECTION (tp_base_channel_get_connection (base)), msg, error);
  lm_message_unref (msg);

  return result;
}

static gboolean
gabble_muc_channel_add_member (GObject *obj,
                               TpHandle handle,
                               const gchar *message,
                               GError **error)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (obj);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  GabbleMucChannelPrivate *priv = self->priv;
  TpGroupMixin *mixin;
  const gchar *jid;

  mixin = TP_GROUP_MIXIN (obj);

  if (handle == mixin->self_handle)
    {
      TpBaseConnection *conn = tp_base_channel_get_connection (base);
      TpIntSet *set_remove_members, *set_remote_pending;
      GArray *arr_members;
      gboolean result;

      /* are we already a member or in remote pending? */
      if (tp_handle_set_is_member (mixin->members, handle) ||
          tp_handle_set_is_member (mixin->remote_pending, handle))
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "already a member or in remote pending");

          return FALSE;
        }

      /* add ourself to remote pending and remove the inviter's
       * main jid from the member list */
      set_remove_members = tp_intset_new ();
      set_remote_pending = tp_intset_new ();

      arr_members = tp_handle_set_to_array (mixin->members);
      if (arr_members->len > 0)
        {
          tp_intset_add (set_remove_members,
              g_array_index (arr_members, guint, 0));
        }
      g_array_free (arr_members, TRUE);

      tp_intset_add (set_remote_pending, handle);

      tp_group_mixin_add_handle_owner (obj, mixin->self_handle,
          conn->self_handle);
      tp_group_mixin_change_members (obj, "", NULL, set_remove_members,
          NULL, set_remote_pending, 0,
          priv->invited
            ? TP_CHANNEL_GROUP_CHANGE_REASON_INVITED
            : TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

      tp_intset_destroy (set_remove_members);
      tp_intset_destroy (set_remote_pending);

      /* seek to enter the room */
      result = send_join_request (self, NULL, error);

      g_object_set (obj, "state",
                    (result) ? MUC_STATE_INITIATED : MUC_STATE_ENDED,
                    NULL);

      /* deny adding */
      tp_group_mixin_change_flags (obj, 0, TP_CHANNEL_GROUP_FLAG_CAN_ADD);

      return result;
    }

  /* check that we're indeed a member when attempting to invite others */
  if (priv->state < MUC_STATE_JOINED)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "channel membership is required for inviting others");

      return FALSE;
    }

  jid = tp_handle_inspect (TP_GROUP_MIXIN (self)->handle_repo, handle);

  return gabble_muc_channel_send_invite (self, jid, message, FALSE, error);
}

static LmHandlerResult
kick_request_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                       LmMessage *reply_msg, GObject *object,
                       gpointer user_data)
{
  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      DEBUG ("Failed to kick user %s from room", (const char *) user_data);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
gabble_muc_channel_remove_member (GObject *obj,
                                  TpHandle handle,
                                  const gchar *message,
                                  GError **error)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (obj);
  TpBaseChannel *base = TP_BASE_CHANNEL (chan);
  GabbleMucChannelPrivate *priv = chan->priv;
  TpGroupMixin *group = TP_GROUP_MIXIN (chan);
  LmMessage *msg;
  LmMessageNode *query_node, *item_node;
  const gchar *jid, *nick;
  gboolean result;

  if (handle == group->self_handle)
    {
      /* User wants to leave the MUC */

      close_channel (chan, message, TRUE, group->self_handle,
          TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
      return TRUE;
    }

  /* Otherwise, the user wants to kick someone. */
  msg = lm_message_new_with_sub_type (priv->jid, LM_MESSAGE_TYPE_IQ,
                                      LM_MESSAGE_SUB_TYPE_SET);

  query_node = lm_message_node_add_child (
      wocky_stanza_get_top_node (msg), "query", NULL);
  lm_message_node_set_attribute (query_node, "xmlns", NS_MUC_ADMIN);

  item_node = lm_message_node_add_child (query_node, "item", NULL);

  jid = tp_handle_inspect (TP_GROUP_MIXIN (obj)->handle_repo, handle);

  nick = strchr (jid, '/');
  if (nick != NULL)
    nick++;

  lm_message_node_set_attributes (item_node,
                                  "nick", nick,
                                  "role", "none",
                                  NULL);

  if (*message != '\0')
    {
      lm_message_node_add_child (item_node, "reason", message);
    }

  DEBUG ("sending MUC kick request for contact %u (%s) to room %s with reason "
      "\"%s\"", handle, jid, priv->jid, message);

  result = _gabble_connection_send_with_reply (
      GABBLE_CONNECTION (tp_base_channel_get_connection (base)),
      msg, kick_request_reply_cb, obj, (gpointer) jid, error);

  lm_message_unref (msg);

  return result;
}


static LmHandlerResult request_config_form_reply_cb (GabbleConnection *conn,
    LmMessage *sent_msg, LmMessage *reply_msg, GObject *object,
    gpointer user_data);

static gboolean
gabble_muc_channel_do_set_properties (GObject *obj,
                                      TpPropertiesContext *ctx,
                                      GError **error)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (obj);
  GabbleMucChannelPrivate *priv = self->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (obj);
  GabbleConnection *conn =
      GABBLE_CONNECTION (tp_base_channel_get_connection (base));
  LmMessage *msg;
  LmMessageNode *node;
  gboolean success;

  g_assert (priv->properties_ctx == NULL);

  /* Changing subject? */
  if (tp_properties_context_has (ctx, ROOM_PROP_SUBJECT))
    {
      const gchar *str;

      str = g_value_get_string (tp_properties_context_get (ctx,
            ROOM_PROP_SUBJECT));

      msg = lm_message_new_with_sub_type (priv->jid,
          LM_MESSAGE_TYPE_MESSAGE, LM_MESSAGE_SUB_TYPE_GROUPCHAT);
      lm_message_node_add_child (
          wocky_stanza_get_top_node (msg), "subject", str);

      success = _gabble_connection_send (conn, msg, error);

      lm_message_unref (msg);

      if (!success)
        return FALSE;
    }

  /* Changing any other properties? */
  if (tp_properties_context_has_other_than (ctx, ROOM_PROP_SUBJECT))
    {
      msg = lm_message_new_with_sub_type (priv->jid,
          LM_MESSAGE_TYPE_IQ, LM_MESSAGE_SUB_TYPE_GET);
      node = lm_message_node_add_child (
          wocky_stanza_get_top_node (msg), "query", NULL);
      lm_message_node_set_attribute (node, "xmlns", NS_MUC_OWNER);

      success = _gabble_connection_send_with_reply (conn, msg,
          request_config_form_reply_cb, G_OBJECT (obj), NULL,
          error);

      lm_message_unref (msg);

      if (!success)
        return FALSE;
    }

  priv->properties_ctx = ctx;
  return TRUE;
}

static LmHandlerResult request_config_form_submit_reply_cb (
    GabbleConnection *conn, LmMessage *sent_msg, LmMessage *reply_msg,
    GObject *object, gpointer user_data);

static LmHandlerResult
request_config_form_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                              LmMessage *reply_msg, GObject *object,
                              gpointer user_data)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (object);
  TpBaseChannel *base = TP_BASE_CHANNEL (chan);
  GabbleMucChannelPrivate *priv = chan->priv;
  TpPropertiesContext *ctx = priv->properties_ctx;
  GError *error = NULL;
  LmMessage *msg = NULL;
  LmMessageNode *submit_node, *form_node, *node;
  guint i, props_left;
  NodeIter j;

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
                           "request for configuration form denied");

      goto OUT;
    }

  form_node = config_form_get_form_node (reply_msg);
  if (form_node == NULL)
    goto PARSE_ERROR;

  /* initialize */
  msg = lm_message_new_with_sub_type (priv->jid, LM_MESSAGE_TYPE_IQ,
                                      LM_MESSAGE_SUB_TYPE_SET);

  node = lm_message_node_add_child (
      wocky_stanza_get_top_node (msg), "query", NULL);
  lm_message_node_set_attribute (node, "xmlns", NS_MUC_OWNER);

  submit_node = lm_message_node_add_child (node, "x", NULL);
  lm_message_node_set_attributes (submit_node,
                                  "xmlns", NS_X_DATA,
                                  "type", "submit",
                                  NULL);

  /* we assume that the number of props will fit in a guint on all supported
   * platforms, so fail at compile time if this is no longer the case
   */
#if NUM_ROOM_PROPS > 32
#error GabbleMUCChannel request_config_form_reply_cb needs porting to TpIntSet
#endif

  props_left = 0;
  for (i = 0; i < NUM_ROOM_PROPS; i++)
    {
      if (i == ROOM_PROP_SUBJECT)
        continue;

      if (tp_properties_context_has (ctx, i))
        props_left |= 1 << i;
    }

  for (j = node_iter (form_node); j; j = node_iter_next (j))
    {
      const gchar *var;
      LmMessageNode *field_node;
      LmMessageNode *child = node_iter_data (j);
      guint id;
      GType type;
      gboolean invert;
      const gchar *val_str = NULL, *type_str;
      gboolean val_bool;

      if (strcmp (child->name, "field") != 0)
        {
          DEBUG ("skipping node '%s'", child->name);
          continue;
        }

      var = lm_message_node_get_attribute (child, "var");
      if (var == NULL) {
        DEBUG ("skipping node '%s' because of lacking var attribute",
               child->name);
        continue;
      }

      id = INVALID_ROOM_PROP;
      type = G_TYPE_BOOLEAN;
      invert = FALSE;

      if (strcmp (var, "anonymous") == 0)
        {
          id = ROOM_PROP_ANONYMOUS;
        }
      else if (strcmp (var, "muc#roomconfig_whois") == 0)
        {
          id = ROOM_PROP_ANONYMOUS;

          if (tp_properties_context_has (ctx, id))
            {
              val_bool = g_value_get_boolean (
                  tp_properties_context_get (ctx, id));
              val_str = (val_bool) ? "moderators" : "anyone";
            }
        }
      else if (strcmp (var, "muc#owner_whois") == 0)
        {
          id = ROOM_PROP_ANONYMOUS;

          if (tp_properties_context_has (ctx, id))
            {
              val_bool = g_value_get_boolean (
                  tp_properties_context_get (ctx, id));
              val_str = (val_bool) ? "admins" : "anyone";
            }
        }
      else if (strcmp (var, "members_only") == 0 ||
               strcmp (var, "muc#roomconfig_membersonly") == 0 ||
               strcmp (var, "muc#owner_inviteonly") == 0)
        {
          id = ROOM_PROP_INVITE_ONLY;
        }
      else if (strcmp (var, "muc#roomconfig_allowinvites") == 0)
        {
          id = ROOM_PROP_INVITE_RESTRICTED;
          invert = TRUE;
        }
      else if (strcmp (var, "moderated") == 0 ||
               strcmp (var, "muc#roomconfig_moderatedroom") == 0 ||
               strcmp (var, "muc#owner_moderatedroom") == 0)
        {
          id = ROOM_PROP_MODERATED;
        }
      else if (strcmp (var, "title") == 0 ||
               strcmp (var, "muc#roomconfig_roomname") == 0 ||
               strcmp (var, "muc#owner_roomname") == 0)
        {
          id = ROOM_PROP_NAME;
          type = G_TYPE_STRING;
        }
      else if (strcmp (var, "muc#roomconfig_roomdesc") == 0 ||
               strcmp (var, "muc#owner_roomdesc") == 0)
        {
          id = ROOM_PROP_DESCRIPTION;
          type = G_TYPE_STRING;
        }
      else if (strcmp (var, "password") == 0 ||
               strcmp (var, "muc#roomconfig_roomsecret") == 0 ||
               strcmp (var, "muc#owner_roomsecret") == 0)
        {
          id = ROOM_PROP_PASSWORD;
          type = G_TYPE_STRING;
        }
      else if (strcmp (var, "password_protected") == 0 ||
               strcmp (var, "muc#roomconfig_passwordprotectedroom") == 0 ||
               strcmp (var, "muc#owner_passwordprotectedroom") == 0)
        {
          id = ROOM_PROP_PASSWORD_REQUIRED;
        }
      else if (strcmp (var, "persistent") == 0 ||
               strcmp (var, "muc#roomconfig_persistentroom") == 0 ||
               strcmp (var, "muc#owner_persistentroom") == 0)
        {
          id = ROOM_PROP_PERSISTENT;
        }
      else if (strcmp (var, "public") == 0 ||
               strcmp (var, "muc#roomconfig_publicroom") == 0 ||
               strcmp (var, "muc#owner_publicroom") == 0)
        {
          id = ROOM_PROP_PRIVATE;
          invert = TRUE;
        }
      else
        {
          DEBUG ("ignoring field '%s'", var);
        }

      /* add the corresponding field node to the reply message */
      field_node = lm_message_node_add_child (submit_node, "field", NULL);
      lm_message_node_set_attribute (field_node, "var", var);

      type_str = lm_message_node_get_attribute (child, "type");
      if (type_str)
        {
          lm_message_node_set_attribute (field_node, "type", type_str);
        }

      if (id != INVALID_ROOM_PROP && tp_properties_context_has (ctx, id))
        {
          /* Known property and we have a value to set */
          DEBUG ("looking up %s... has=%d", room_property_signatures[id].name,
              tp_properties_context_has (ctx, id));

          if (!val_str)
            {
              const GValue *provided_value;

              provided_value = tp_properties_context_get (ctx, id);

              switch (type) {
                case G_TYPE_BOOLEAN:
                  val_bool = g_value_get_boolean (provided_value);
                  if (invert)
                    val_bool = !val_bool;
                  val_str = val_bool ? "1" : "0";
                  break;
                case G_TYPE_STRING:
                  val_str = g_value_get_string (provided_value);
                  break;
                default:
                  g_assert_not_reached ();
              }
            }

          DEBUG ("Setting value %s for %s", val_str, var);

          props_left &= ~(1 << id);

          /* add the corresponding value node(s) to the reply message */
          lm_message_node_add_child (field_node, "value", val_str);
        }
      else
        {
          /* Copy all the <value> nodes */
          NodeIter k;

          for (k = node_iter (child); k; k = node_iter_next (k))
            {
              LmMessageNode *value_node = node_iter_data (k);

              if (tp_strdiff (value_node->name, "value"))
                /* Not a value, skip it */
                continue;

              lm_message_node_add_child (field_node, "value",
                  lm_message_node_get_value (value_node));
            }
        }
    }

  if (props_left != 0)
    {
      printf (TP_ANSI_BOLD_ON TP_ANSI_FG_WHITE TP_ANSI_BG_RED
              "\n%s: the following properties were not substituted:\n",
              G_STRFUNC);

      for (i = 0; i < NUM_ROOM_PROPS; i++)
        {
          if ((props_left & (1 << i)) != 0)
            {
              printf ("  %s\n", room_property_signatures[i].name);
            }
        }

      printf ("\nthis is a MUC server compatibility bug in gabble, please "
              "report it with a full debug log attached (running gabble "
              "with LM_DEBUG=net)" TP_ANSI_RESET "\n\n");
      fflush (stdout);

      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                           "not all properties were substituted");
      goto OUT;
    }

  _gabble_connection_send_with_reply (
      GABBLE_CONNECTION (tp_base_channel_get_connection (base)), msg,
      request_config_form_submit_reply_cb, G_OBJECT (object),
      NULL, &error);

  goto OUT;

PARSE_ERROR:
  error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                       "error parsing reply from server");

OUT:
  if (error)
    {
      tp_properties_context_return (ctx, error);
      priv->properties_ctx = NULL;
    }

  if (msg)
    lm_message_unref (msg);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static LmHandlerResult
request_config_form_submit_reply_cb (GabbleConnection *conn,
                                     LmMessage *sent_msg,
                                     LmMessage *reply_msg,
                                     GObject *object,
                                     gpointer user_data)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = chan->priv;
  TpPropertiesContext *ctx = priv->properties_ctx;
  GError *error = NULL;
  gboolean returned;

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
                           "submitted configuration form was rejected");
    }

  if (!error)
    {
      guint i;

      for (i = 0; i < NUM_ROOM_PROPS; i++)
        {
          if (i != ROOM_PROP_SUBJECT)
            tp_properties_context_remove (ctx, i);
        }

      returned = tp_properties_context_return_if_done (ctx);
    }
  else
    {
      tp_properties_context_return (ctx, error);
      returned = TRUE;

      /* Get the properties into a consistent state. */
      room_properties_update (chan);
    }

  if (returned)
    priv->properties_ctx = NULL;

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/**
 * gabble_muc_channel_set_chat_state
 *
 * Implements D-Bus method SetChatState
 * on interface org.freedesktop.Telepathy.Channel.Interface.ChatState
 */
static void
gabble_muc_channel_set_chat_state (TpSvcChannelInterfaceChatState *iface,
                                   guint state,
                                   DBusGMethodInvocation *context)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (iface);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  GabbleMucChannelPrivate *priv;
  GError *error = NULL;

  g_assert (GABBLE_IS_MUC_CHANNEL (self));

  priv = self->priv;

  if (state >= NUM_TP_CHANNEL_CHAT_STATES)
    {
      DEBUG ("invalid state %u", state);

      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "invalid state: %u", state);
    }

  if (state == TP_CHANNEL_CHAT_STATE_GONE)
    {
      /* We cannot explicitly set the Gone state */
      DEBUG ("you may not explicitly set the Gone state");

      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "you may not explicitly set the Gone state");
    }

  if (error != NULL ||
      !gabble_message_util_send_chat_state (G_OBJECT (self),
          GABBLE_CONNECTION (tp_base_channel_get_connection (base)),
          LM_MESSAGE_SUB_TYPE_GROUPCHAT, state, priv->jid, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return;
    }

  tp_svc_channel_interface_chat_state_return_from_set_chat_state (context);
}

gboolean
gabble_muc_channel_send_presence (GabbleMucChannel *self,
                                  GError **error)
{
  GabbleMucChannelPrivate *priv = self->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  WockyStanza *stanza;
  gboolean result;

  /* do nothing if we havn't actually joined yet */
  if (priv->state < MUC_STATE_INITIATED)
    return TRUE;

  stanza = wocky_muc_create_presence (priv->wmuc,
      WOCKY_STANZA_SUB_TYPE_NONE, NULL, NULL);
  result = _gabble_connection_send (
      GABBLE_CONNECTION (tp_base_channel_get_connection (base)),
      (LmMessage *) stanza, error);

  g_object_unref (stanza);
  return result;
}

GabbleTubesChannel *
gabble_muc_channel_open_tube (GabbleMucChannel *gmuc,
    TpHandle initiator,
    gboolean requested)
{
  GabbleMucChannelPrivate *priv = gmuc->priv;

  if (priv->tube == NULL)
    priv->tube = new_tube (gmuc, initiator, requested);

  if (priv->tube != NULL)
    return g_object_ref (priv->tube);

  return NULL;
}

void
gabble_muc_channel_close_tube (GabbleMucChannel *gmuc)
{
  GabbleMucChannelPrivate *priv = gmuc->priv;

  if (priv->tube != NULL)
    {
      TpHandle room;
      GabbleTubesChannel *tube = priv->tube;

      priv->tube = NULL;
      g_object_get (tube, "handle", &room, NULL);
      DEBUG ("removing MUC tubes channel with handle %d", room);
      gabble_tubes_channel_close (tube);
      g_object_unref (tube);
    }
}

GabbleCallMucChannel *
gabble_muc_channel_get_call (GabbleMucChannel *gmuc)
{
  return gmuc->priv->call;
}

GList *
gabble_muc_channel_get_call_channels (GabbleMucChannel *self)
{
  return self->priv->calls;
}

static void
muc_channel_call_ended_cb (GabbleCallMucChannel *muc, gpointer user_data)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (user_data);

  g_assert (self->priv->call == muc);
  self->priv->call = NULL;
}

static void
muc_channel_call_closed_cb (GabbleCallMucChannel *muc, gpointer user_data)
{
  GabbleMucChannel *gmuc = GABBLE_MUC_CHANNEL (user_data);
  GabbleMucChannelPrivate *priv = gmuc->priv;

  g_assert (g_list_find (priv->calls, muc) != NULL);

  /* closed the active muc */
  if (priv->call == muc)
    priv->call = NULL;

  priv->calls = g_list_remove (priv->calls, muc);
  g_object_unref (muc);
}

static void
muc_call_channel_finish_requests (GabbleMucChannel *self,
  GabbleCallMucChannel *call,
  GError *error)
{
  GList *l;

  if (call != NULL)
    {
      GSList *requests = NULL;

      DEBUG ("Call channel created");

      for (l = self->priv->call_requests ; l != NULL; l = g_list_next (l))
        requests = g_slist_append (requests,
          g_simple_async_result_get_op_res_gpointer (
            G_SIMPLE_ASYNC_RESULT(l->data)));

      g_signal_emit (self, signals[NEW_CALL], 0, call, requests);
      g_slist_free (requests);
    }
  else
    {
      DEBUG ("Failed to create call channel: %s", error->message);
    }

  for (l = self->priv->call_requests ; l != NULL; l = g_list_next (l))
    {
      GSimpleAsyncResult *r = G_SIMPLE_ASYNC_RESULT (l->data);

      if (error != NULL)
        g_simple_async_result_set_from_error (r, error);

      g_simple_async_result_complete (r);
      g_object_unref (r);
    }

  g_list_free (self->priv->call_requests);
  self->priv->call_requests = NULL;
}

static void
muc_channel_call_channel_done_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleMucChannel *gmuc = GABBLE_MUC_CHANNEL (user_data);
  GabbleMucChannelPrivate *priv = gmuc->priv;
  GError *error = NULL;

  g_assert (priv->call == NULL);
  if (tp_base_channel_is_destroyed (TP_BASE_CHANNEL (gmuc)))
    goto out;

  priv->call_initiating = FALSE;
  priv->call = gabble_call_muc_channel_new_finish (source,
    result, &error);

  if (priv->call == NULL)
    goto error;

  priv->calls = g_list_prepend (priv->calls, priv->call);

  g_signal_connect (priv->call, "closed",
    G_CALLBACK (muc_channel_call_closed_cb),
    gmuc);

  g_signal_connect (priv->call, "ended",
    G_CALLBACK (muc_channel_call_ended_cb),
    gmuc);

error:
  muc_call_channel_finish_requests (gmuc, priv->call, error);

  g_clear_error (&error);

out:
  /* we kept ourselves reffed while the call channel was being created */
  g_object_unref (gmuc);
}

static void
gabble_muc_channel_start_call_creation (GabbleMucChannel *gmuc,
  GHashTable *request)
{
  GabbleMucChannelPrivate *priv = gmuc->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (gmuc);

  g_assert (!priv->call_initiating);
  g_assert (priv->call == NULL);

  priv->call_initiating = TRUE;

  /* Keep ourselves reffed while call channels are created */
  g_object_ref (gmuc);
  gabble_call_muc_channel_new_async (
      GABBLE_CONNECTION (tp_base_channel_get_connection (base)),
      priv->requests_cancellable,
      tp_base_channel_get_object_path (base),
      gmuc,
      tp_base_channel_get_target_handle (base),
      request,
      muc_channel_call_channel_done_cb,
      gmuc);
}

void
gabble_muc_channel_request_call (GabbleMucChannel *gmuc,
    GHashTable *request,
    gboolean require_new,
    gpointer token,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleMucChannelPrivate *priv = gmuc->priv;
  GSimpleAsyncResult *res;

  /* FIXME: Ponder whether this function should even be used when the call
   * already exists and to have the return indicate that it was already
   * satisfied (instead of a newly created channel) */
  g_assert (priv->call == NULL);

  if (require_new && priv->call_initiating)
    {
      g_simple_async_report_error_in_idle (G_OBJECT (gmuc),
        callback, user_data,
        TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "A request for a call is already in progress");
      return;
    }

  if (!priv->call_initiating)
    gabble_muc_channel_start_call_creation (gmuc, request);

  res = g_simple_async_result_new (G_OBJECT (gmuc),
      callback, user_data, gabble_muc_channel_request_call_finish);
  g_simple_async_result_set_op_res_gpointer (res, token, NULL);

  priv->call_requests = g_list_append (priv->call_requests, res);
}

gboolean
gabble_muc_channel_request_call_finish (GabbleMucChannel *gmuc,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
      error))
    return FALSE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
    G_OBJECT (gmuc), gabble_muc_channel_request_call_finish), FALSE);

  return TRUE;
}


gboolean
gabble_muc_channel_handle_jingle_session (GabbleMucChannel *self,
    GabbleJingleSession *session)
{
  GabbleMucChannelPrivate *priv = self->priv;

  /* No Muji no need to handle call sessions */
  if (priv->call == NULL)
    return FALSE;

  gabble_call_muc_channel_incoming_session (priv->call, session);

  return TRUE;
}

void
gabble_muc_channel_teardown (GabbleMucChannel *gmuc)
{
  close_channel (gmuc, NULL, FALSE, 0, 0);
}

static void
password_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfacePasswordClass *klass =
    (TpSvcChannelInterfacePasswordClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_password_implement_##x (\
    klass, gabble_muc_channel_##x)
  IMPLEMENT(get_password_flags);
  IMPLEMENT(provide_password);
#undef IMPLEMENT
}

static void
chat_state_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceChatStateClass *klass =
    (TpSvcChannelInterfaceChatStateClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_chat_state_implement_##x (\
    klass, gabble_muc_channel_##x)
  IMPLEMENT(set_chat_state);
#undef IMPLEMENT
}
