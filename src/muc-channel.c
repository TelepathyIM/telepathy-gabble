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
#include <wocky/wocky-utils.h>
#include <wocky/wocky-xmpp-error.h>

#include <dbus/dbus-glib.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/debug-ansi.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>

#define DEBUG_FLAG GABBLE_DEBUG_MUC
#include "connection.h"
#include "conn-aliasing.h"
#include "conn-util.h"
#include "debug.h"
#include "disco.h"
#include "error.h"
#include "message-util.h"
#include "room-config.h"
#include "namespaces.h"
#include "presence.h"
#include "util.h"
#include "presence-cache.h"
#include "call-muc-channel.h"
#include "gabble-signals-marshal.h"
#include "gabble-enumtypes.h"

#define DEFAULT_JOIN_TIMEOUT 180
#define DEFAULT_LEAVE_TIMEOUT 180
#define MAX_NICK_RETRIES 3

#define PROPS_POLL_INTERVAL_LOW  60 * 5
#define PROPS_POLL_INTERVAL_HIGH 60

static void password_iface_init (gpointer, gpointer);
static void chat_state_iface_init (gpointer, gpointer);
static void subject_iface_init (gpointer, gpointer);
static void gabble_muc_channel_start_call_creation (GabbleMucChannel *gmuc,
    GHashTable *request);
static void muc_call_channel_finish_requests (GabbleMucChannel *self,
    GabbleCallMucChannel *call,
    GError *error);

G_DEFINE_TYPE_WITH_CODE (GabbleMucChannel, gabble_muc_channel,
    TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
      tp_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_PASSWORD,
      password_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT,
      tp_message_mixin_text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MESSAGES,
      tp_message_mixin_messages_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_CHAT_STATE,
      chat_state_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_CONFERENCE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_ROOM, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_ROOM_CONFIG,
      gabble_room_config_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_SUBJECT,
      subject_iface_init);
    )

static void gabble_muc_channel_send (GObject *obj, TpMessage *message,
    TpMessageSendingFlags flags);
static void gabble_muc_channel_close (TpBaseChannel *base);

static const gchar *gabble_muc_channel_interfaces[] = {
    TP_IFACE_CHANNEL_INTERFACE_GROUP,
    TP_IFACE_CHANNEL_INTERFACE_PASSWORD,
    TP_IFACE_CHANNEL_INTERFACE_CHAT_STATE,
    TP_IFACE_CHANNEL_INTERFACE_MESSAGES,
    TP_IFACE_CHANNEL_INTERFACE_CONFERENCE,
    TP_IFACE_CHANNEL_INTERFACE_ROOM,
    TP_IFACE_CHANNEL_INTERFACE_ROOM_CONFIG,
    TP_IFACE_CHANNEL_INTERFACE_SUBJECT,
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
  PROP_ROOM_NAME,
  PROP_SERVER,

  PROP_SUBJECT,
  PROP_SUBJECT_ACTOR,
  PROP_SUBJECT_TIMESTAMP,
  PROP_CAN_SET_SUBJECT,

  LAST_PROPERTY
};

static const gchar *muc_states[] =
{
  "MUC_STATE_CREATED",
  "MUC_STATE_INITIATED",
  "MUC_STATE_AUTH",
  "MUC_STATE_JOINED",
  "MUC_STATE_ENDED",
};

/* private structures */
struct _GabbleMucChannelPrivate
{
  GabbleMucState state;
  gboolean closing;

  guint join_timer_id;
  guint poll_timer_id;
  guint leave_timer_id;

  gboolean must_provide_password;
  DBusGMethodInvocation *password_ctx;

  const gchar *jid;
  gboolean requested;

  guint nick_retry_count;
  GString *self_jid;
  WockyMucRole self_role;
  WockyMucAffiliation self_affil;

  guint recv_id;

  GabbleRoomConfig *room_config;
  GHashTable *properties_being_updated;

  /* Room interface */
  gchar *room_name;
  gchar *server;

  /* Subject interface */
  gchar *subject;
  gchar *subject_actor;
  gint64 subject_timestamp;
  gboolean can_set_subject;
  DBusGMethodInvocation *set_subject_context;
  gchar *set_subject_stanza_id;

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

typedef struct {
  GabbleMucChannel *channel;
  TpMessage *message;
  gchar *token;
} _GabbleMUCSendMessageCtx;

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

/*  signatures for presence handlers */

static void handle_fill_presence (WockyMuc *muc,
    WockyStanza *stanza,
    gpointer user_data);

static void handle_renamed (GObject *source,
    WockyStanza *stanza,
    guint codes,
    gpointer data);

static void handle_error (GObject *source,
    WockyStanza *stanza,
    WockyXmppError errnum,
    const gchar *message,
    gpointer data);

static void handle_join (WockyMuc *muc,
    WockyStanza *stanza,
    guint codes,
    gpointer data);

static void handle_parted (GObject *source,
    WockyStanza *stanza,
    guint codes,
    const gchar *actor_jid,
    const gchar *why,
    const gchar *msg,
    gpointer data);

static void handle_left (GObject *source,
    WockyStanza *stanza,
    guint codes,
    WockyMucMember *who,
    const gchar *actor_jid,
    const gchar *why,
    const gchar *msg,
    gpointer data);

static void handle_presence (GObject *source,
    WockyStanza *stanza,
    guint codes,
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

/* Signatures for some other stuff. */

static void _gabble_muc_channel_handle_subject (GabbleMucChannel *chan,
    TpHandleType handle_type,
    TpHandle sender, time_t timestamp, const gchar *subject, LmMessage *msg);
static void _gabble_muc_channel_receive (GabbleMucChannel *chan,
    TpChannelTextMessageType msg_type, TpHandleType handle_type,
    TpHandle sender, time_t timestamp, const gchar *id, const gchar *text,
    LmMessage *msg, TpChannelTextSendError send_error,
    TpDeliveryStatus delivery_status);

static void
gabble_muc_channel_constructed (GObject *obj)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (obj);
  GabbleMucChannelPrivate *priv = self->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  TpHandleRepoIface *room_handles, *contact_handles;
  TpHandle target, initiator, self_handle;
  gchar *tmp;
  TpChannelTextMessageType types[] = {
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
  gboolean ok;

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
  priv->self_role = WOCKY_MUC_ROLE_NONE;
  priv->self_affil = WOCKY_MUC_AFFILIATION_NONE;

  /* initialise the wocky muc object */
  {
    GabbleConnection *conn = GABBLE_CONNECTION (base_conn);
    WockyPorter *porter = gabble_connection_dup_porter (conn);
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

  /* initialize message mixin */
  tp_message_mixin_init (obj, G_STRUCT_OFFSET (GabbleMucChannel, message_mixin),
      base_conn);
  tp_message_mixin_implement_sending (obj, gabble_muc_channel_send,
      G_N_ELEMENTS (types), types, 0,
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_FAILURES |
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_SUCCESSES,
      supported_content_types);

  tp_group_mixin_add_handle_owner (obj, self_handle, base_conn->self_handle);

  /* Room interface */
  g_object_get (self,
      "target-id", &tmp,
      NULL);

  if (priv->room_name != NULL)
    ok = gabble_decode_jid (tmp, NULL, &(priv->server), NULL);
  else
    ok = gabble_decode_jid (tmp, &(priv->room_name), &(priv->server), NULL);
  g_free (tmp);

  /* Asserting here is fine because the target ID has already been
   * checked so we know it's valid. */
  g_assert (ok);

  priv->subject = NULL;
  priv->subject_actor = NULL;
  priv->subject_timestamp = 0;
  /* fd.o#13157: The subject is currently assumed to be modifiable by everyone
   * in the room (role >= VISITOR). When that bug is fixed, it will be: */
  /* Modifiable via special <message/>s, if the user's role is high enough;
   * "high enough" is defined by the muc#roominfo_changesubject and
   * muc#roomconfig_changesubject settings. */
  priv->can_set_subject = TRUE;

  {
    GabbleRoomConfigProperty mutable_properties[] = {
        GABBLE_ROOM_CONFIG_ANONYMOUS,
        GABBLE_ROOM_CONFIG_INVITE_ONLY,
        GABBLE_ROOM_CONFIG_MODERATED,
        GABBLE_ROOM_CONFIG_TITLE,
        GABBLE_ROOM_CONFIG_PERSISTENT,
        GABBLE_ROOM_CONFIG_PRIVATE,
        GABBLE_ROOM_CONFIG_PASSWORD_PROTECTED,
        GABBLE_ROOM_CONFIG_PASSWORD,
    };
    guint i;

    priv->room_config = gabble_room_config_new ((TpBaseChannel *) self);
    for (i = 0; i < G_N_ELEMENTS (mutable_properties); i++)
      gabble_room_config_set_property_mutable (priv->room_config,
          mutable_properties[i], TRUE);

    /* Just to get those mutable properties out there. */
    gabble_room_config_emit_properties_changed (priv->room_config);
  }

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

typedef struct {
    const gchar *var;
    const gchar *config_property_name;
    gboolean value;
} FeatureMapping;

static FeatureMapping *
lookup_feature (const gchar *var)
{
  static FeatureMapping features[] = {
      { "muc_nonanonymous", "anonymous", FALSE },
      { "muc_semianonymous", "anonymous", TRUE },
      { "muc_anonymous", "anonymous", TRUE },

      { "muc_open", "invite-only", FALSE },
      { "muc_membersonly", "invite-only", TRUE },

      { "muc_unmoderated", "moderated", FALSE },
      { "muc_moderated", "moderated", TRUE },

      { "muc_unsecure", "password-protected", FALSE },
      { "muc_unsecured", "password-protected", FALSE },
      { "muc_passwordprotected", "password-protected", TRUE },

      { "muc_temporary", "persistent", FALSE },
      { "muc_persistent", "persistent", TRUE },

      { "muc_public", "private", FALSE },
      { "muc_hidden", "private", TRUE },

      /* The MUC namespace is included as a feature in disco results. We ignore
       * it here.
       */
      { NS_MUC, NULL, FALSE },

      { NULL }
  };
  FeatureMapping *f;

  for (f = features; f->var != NULL; f++)
    if (strcmp (var, f->var) == 0)
      return f;

  return NULL;
}

static const gchar *
map_feature (
    WockyNode *feature,
    GValue *value)
{
  const gchar *var = wocky_node_get_attribute (feature, "var");
  FeatureMapping *f;

  if (var == NULL)
    return NULL;

  f = lookup_feature (var);

  if (f == NULL)
    {
      DEBUG ("unhandled feature '%s'", var);
      return NULL;
    }

  if (f->config_property_name != NULL)
    {
      g_value_init (value, G_TYPE_BOOLEAN);
      g_value_set_boolean (value, f->value);
    }

  return f->config_property_name;
}

static const gchar *
handle_form (
    WockyNode *x,
    GValue *value)
{
  WockyNodeIter j;
  WockyNode *field;

  wocky_node_iter_init (&j, x, "field", NULL);
  while (wocky_node_iter_next (&j, &field))
    {
      const gchar *var = wocky_node_get_attribute (field, "var");
      const gchar *description;

      if (tp_strdiff (var, "muc#roominfo_description"))
        continue;

      description = wocky_node_get_content_from_child (field, "value");
      g_value_init (value, G_TYPE_STRING);
      g_value_set_string (value, description != NULL ? description : "");
      return "description";
    }

  return NULL;
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
  GabbleMucChannelPrivate *priv = chan->priv;
  LmMessageNode *lm_node;
  NodeIter i;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  if (error)
    {
      DEBUG ("got error %s", error->message);
      return;
    }

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
          g_object_set (priv->room_config, "title", name, NULL);
        }
    }

  for (i = node_iter (query_result); i; i = node_iter_next (i))
    {
      const gchar *config_property_name = NULL;
      LmMessageNode *child = node_iter_data (i);
      GValue val = { 0, };

      if (strcmp (child->name, "feature") == 0)
        {
          config_property_name = map_feature (child, &val);
        }
      else if (strcmp (child->name, "x") == 0 &&
               lm_message_node_has_namespace (child, NS_X_DATA, NULL))
        {
          config_property_name = handle_form (child, &val);
        }

      if (config_property_name != NULL)
        {
          g_object_set_property ((GObject *) priv->room_config, config_property_name, &val);

          g_value_unset (&val);
        }
    }

  /* This could be the first time we've fetched the room properties, or it
   * could be a later time; either way, this method does the right thing.
   */
  gabble_room_config_set_retrieved (priv->room_config);
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

static void
send_join_request (GabbleMucChannel *gmuc)
{
  GabbleMucChannelPrivate *priv = gmuc->priv;

  wocky_muc_join (priv->wmuc, NULL);
}

static gboolean
timeout_leave (gpointer data)
{
  GabbleMucChannel *chan = data;

  DEBUG ("leave timed out (we never got our unavailable presence echoed "
      "back to us by the conf server), closing channel now");

  tp_base_channel_destroyed (TP_BASE_CHANNEL (chan));

  return FALSE;
}

static void
send_leave_message (GabbleMucChannel *gmuc,
                    const gchar *reason)
{
  GabbleMucChannelPrivate *priv = gmuc->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (gmuc);
  WockyStanza *stanza = wocky_muc_create_presence (priv->wmuc,
      WOCKY_STANZA_SUB_TYPE_UNAVAILABLE, reason);

  g_signal_emit (gmuc, signals[PRE_PRESENCE], 0, stanza);
  _gabble_connection_send (
      GABBLE_CONNECTION (tp_base_channel_get_connection (base)), stanza, NULL);

  g_object_unref (stanza);

  priv->leave_timer_id =
    g_timeout_add_seconds (DEFAULT_LEAVE_TIMEOUT, timeout_leave, gmuc);
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
    case PROP_ROOM_NAME:
      g_value_set_string (value, priv->room_name);
      break;
    case PROP_SERVER:
      g_value_set_string (value, priv->server);
      break;
    case PROP_SUBJECT:
      g_value_set_string (value, priv->subject);
      break;
    case PROP_SUBJECT_ACTOR:
      g_value_set_string (value, priv->subject_actor);
      break;
    case PROP_SUBJECT_TIMESTAMP:
      g_value_set_int64 (value, priv->subject_timestamp);
      break;
    case PROP_CAN_SET_SUBJECT:
      g_value_set_boolean (value, priv->can_set_subject);
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
    case PROP_ROOM_NAME:
      priv->room_name = g_value_dup_string (value);
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
static void gabble_muc_channel_update_configuration_async (
    TpBaseChannel *base,
    GHashTable *validated_properties,
    GAsyncReadyCallback callback,
    gpointer user_data);

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
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessagePartSupportFlags",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "DeliveryReportingSupport",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "SupportedContentTypes",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessageTypes",
      TP_IFACE_CHANNEL_INTERFACE_ROOM, "RoomName",
      TP_IFACE_CHANNEL_INTERFACE_ROOM, "Server",
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
  static TpDBusPropertiesMixinPropImpl room_props[] = {
      { "RoomName", "room-name", NULL, },
      { "Server", "server", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinPropImpl subject_props[] = {
      { "Subject", "subject", NULL },
      { "Actor", "subject-actor", NULL },
      { "Timestamp", "subject-timestamp", NULL },
      { "CanSet", "can-set-subject", NULL },
      { NULL }
  };

  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
    { TP_IFACE_CHANNEL_INTERFACE_CONFERENCE,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      conference_props,
    },
    { TP_IFACE_CHANNEL_INTERFACE_ROOM,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      room_props,
    },
    { TP_IFACE_CHANNEL_INTERFACE_SUBJECT,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      subject_props,
    },
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

  param_spec = g_param_spec_string ("room-name",
      "RoomName",
      "The human-readable identifier of a chat room.",
      "",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ROOM_NAME,
      param_spec);

  param_spec = g_param_spec_string ("server",
      "Server",
      "the DNS name of the server hosting this channel",
      "",
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SERVER,
      param_spec);

  param_spec = g_param_spec_string ("subject",
      "Subject.Subject", "The subject of the room",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SUBJECT, param_spec);

  param_spec = g_param_spec_string ("subject-actor",
      "Subject.Actor", "The JID of the contact who last changed the subject",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SUBJECT_ACTOR,
      param_spec);

  param_spec = g_param_spec_int64 ("subject-timestamp",
      "Subject.Timestamp",
      "The UNIX timestamp at which the subject was last changed",
      G_MININT64, G_MAXINT64, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SUBJECT_TIMESTAMP,
      param_spec);

  param_spec = g_param_spec_boolean ("can-set-subject",
      "Subject.CanSet", "Whether we believe we can set the subject",
      TRUE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CAN_SET_SUBJECT,
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

  gabble_muc_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleMucChannelClass, dbus_props_class));

  tp_message_mixin_init_dbus_properties (object_class);
  gabble_room_config_register_class (base_class);

  tp_group_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleMucChannelClass, group_class),
      gabble_muc_channel_add_member,
      gabble_muc_channel_remove_member);
  tp_group_mixin_init_dbus_properties (object_class);
  tp_group_mixin_class_allow_self_removal (object_class);

  {
    /* TODO: monkeypatching is Bad And Wrong! */
    GabbleRoomConfigClass *config_class = g_type_class_ref (GABBLE_TYPE_ROOM_CONFIG);

    config_class->update_async = gabble_muc_channel_update_configuration_async;
    g_type_class_unref (config_class);
  }
}

static void clear_join_timer (GabbleMucChannel *chan);
static void clear_poll_timer (GabbleMucChannel *chan);
static void clear_leave_timer (GabbleMucChannel *chan);

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
  clear_leave_timer (self);

  tp_clear_object (&priv->wmuc);
  tp_clear_object (&priv->requests_cancellable);
  tp_clear_object (&priv->room_config);

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

  g_free (priv->room_name);
  g_free (priv->server);
  g_free (priv->subject);
  g_free (priv->subject_actor);

  tp_group_mixin_finalize (object);
  tp_message_mixin_finalize (object);

  G_OBJECT_CLASS (gabble_muc_channel_parent_class)->finalize (object);
}

static void
clear_join_timer (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv = chan->priv;

  if (priv->join_timer_id != 0)
    {
      g_source_remove (priv->join_timer_id);
      priv->join_timer_id = 0;
    }
}

static void
clear_poll_timer (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv = chan->priv;

  if (priv->poll_timer_id != 0)
    {
      g_source_remove (priv->poll_timer_id);
      priv->poll_timer_id = 0;
    }
}

static void
clear_leave_timer (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv = chan->priv;

  if (priv->leave_timer_id != 0)
    {
      g_source_remove (priv->leave_timer_id);
      priv->leave_timer_id = 0;
    }
}

static void
change_must_provide_password (
    GabbleMucChannel *chan,
    gboolean must_provide_password)
{
  GabbleMucChannelPrivate *priv;
  TpChannelPasswordFlags added, removed;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = chan->priv;

  if (priv->must_provide_password == !!must_provide_password)
    return;

  priv->must_provide_password = !!must_provide_password;

  if (must_provide_password)
    {
      added = TP_CHANNEL_PASSWORD_FLAG_PROVIDE;
      removed = 0;
    }
  else
    {
      added = 0;
      removed = TP_CHANNEL_PASSWORD_FLAG_PROVIDE;
    }

  DEBUG ("emitting password flags changed, added 0x%X, removed 0x%X",
          added, removed);

  tp_svc_channel_interface_password_emit_password_flags_changed (
      chan, added, removed);
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
      change_must_provide_password (chan, FALSE);
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
return_from_set_subject (
    GabbleMucChannel *self,
    const GError *error)
{
  GabbleMucChannelPrivate *priv = self->priv;

  if (error == NULL)
    tp_svc_channel_interface_subject_return_from_set_subject (
        priv->set_subject_context);
  else
    dbus_g_method_return_error (priv->set_subject_context, error);

  priv->set_subject_context = NULL;
  tp_clear_pointer (&priv->set_subject_stanza_id, g_free);
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
  GError error = { TP_ERRORS, TP_ERROR_CANCELLED,
      "Muc channel closed below us" };

  if (tp_base_channel_is_destroyed (base) || priv->closing)
    return;

  DEBUG ("Closing");
  /* Ensure we stay alive even while telling everyone else to abandon us. */
  g_object_ref (chan);

  gabble_muc_channel_close_tube (chan);
  muc_call_channel_finish_requests (chan, NULL, &error);
  g_cancellable_cancel (priv->requests_cancellable);

  while (priv->calls != NULL)
    tp_base_channel_close (TP_BASE_CHANNEL (priv->calls->data));

  set = tp_intset_new_containing (TP_GROUP_MIXIN (chan)->self_handle);
  tp_group_mixin_change_members ((GObject *) chan, reason,
      NULL, set, NULL, NULL, actor, reason_code);
  tp_intset_destroy (set);

  /* If we're currently in the MUC, tell it we're leaving and wait for a reply;
   * handle_parted() will call tp_base_channel_destroyed() and all the Closed
   * signals will be emitted. (Since there's no waiting-for-password state on
   * the protocol level, MUC_STATE_AUTH doesn't count as ‘in the MUC’.) See
   * fd.o#19930 for more details.
   */
  if (inform_muc && priv->state >= MUC_STATE_INITIATED
      && priv->state != MUC_STATE_AUTH)
    {
      send_leave_message (chan, reason);
      priv->closing = TRUE;
    }
  else
    {
      tp_base_channel_destroyed (base);
    }

  handles = tp_handle_set_to_array (chan->group.members);
  gabble_presence_cache_update_many (conn->presence_cache, handles,
    NULL, GABBLE_PRESENCE_UNKNOWN, NULL, 0);
  g_array_free (handles, TRUE);

  if (priv->set_subject_context != NULL)
    return_from_set_subject (chan, &error);

  g_object_set (chan, "state", MUC_STATE_ENDED, NULL);
  g_object_unref (chan);
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
                      WockyStanza *stanza,
                      GError **tp_error)
{
  GabbleMucChannelPrivate *priv = chan->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (chan);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (chan);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      tp_base_channel_get_connection (base), TP_HANDLE_TYPE_CONTACT);
  TpHandle self_handle;
  TpIntSet *add_rp, *remove_rp;
  const gchar *from = wocky_stanza_get_from (stanza);

  /* If this is a nick conflict message with a resource in the JID, and the
   * resource doesn't match the one we're currently trying to join as, then
   * ignore it. This works around a bug in Google Talk's MUC server, which
   * sends the conflict message twice. It's valid for there to be no resource
   * in the from='' field. If Google didn't include the resource, we couldn't
   * work around the bug; but they happen to do so, so yay.
   * <https://bugs.freedesktop.org/show_bug.cgi?id=35619>
   *
   * FIXME: WockyMuc should provide a _join_async() method and do all this for
   * us.
   */
  g_assert (from != NULL);

  if (index (from, '/') != NULL && tp_strdiff (from, priv->self_jid->str))
    {
      DEBUG ("ignoring spurious conflict message for %s", from);
      return TRUE;
    }

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
  send_join_request (chan);
  return TRUE;
}

static void
room_created_submit_reply_cb (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  if (conn_util_send_iq_finish (GABBLE_CONNECTION (source), result, NULL, NULL))
    DEBUG ("failed to submit room config");
}

static WockyNode *
config_form_get_form_node (WockyStanza *stanza)
{
  WockyNode *query, *x;
  WockyNodeIter i;

  /* find the query node */
  query = wocky_node_get_child (wocky_stanza_get_top_node (stanza), "query");
  if (query == NULL)
    return NULL;

  /* then the form node */
  wocky_node_iter_init (&i, query, "x", NS_X_DATA);
  while (wocky_node_iter_next (&i, &x))
    {
      if (!tp_strdiff (wocky_node_get_attribute (x, "type"), "form"))
        return x;
    }

  return NULL;
}

static void
perms_config_form_reply_cb (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (user_data);
  GabbleMucChannelPrivate *priv = self->priv;
  WockyStanza *reply = NULL;
  WockyNode *form_node, *field;
  WockyNodeIter i;

  if (!conn_util_send_iq_finish (GABBLE_CONNECTION (source), result, &reply, NULL))
    {
      DEBUG ("request for config form failed, property permissions "
                 "will be inaccurate");
      goto OUT;
    }

  /* just in case our affiliation has changed in the meantime */
  if (priv->self_affil != WOCKY_MUC_AFFILIATION_OWNER)
    goto OUT;

  form_node = config_form_get_form_node (reply);
  if (form_node == NULL)
    {
      DEBUG ("form node not found, property permissions will be inaccurate");
      goto OUT;
    }

  wocky_node_iter_init (&i, form_node, "field", NULL);
  while (wocky_node_iter_next (&i, &field))
    {
      const gchar *var = wocky_node_get_attribute (field, "var");

      if (!tp_strdiff (var, "muc#roomconfig_roomdesc") ||
          !tp_strdiff (var, "muc#owner_roomdesc"))
        {
          gabble_room_config_set_property_mutable (priv->room_config,
              GABBLE_ROOM_CONFIG_DESCRIPTION, TRUE);
          gabble_room_config_emit_properties_changed (priv->room_config);
          break;
        }
    }

OUT:
  tp_clear_object (&reply);
  g_object_unref (self);
}

static void
emit_subject_changed (GabbleMucChannel *chan)
{
  const gchar *changed[] = { "Subject", "Actor", "Timestamp", NULL };

  tp_dbus_properties_mixin_emit_properties_changed (G_OBJECT (chan),
      TP_IFACE_CHANNEL_INTERFACE_SUBJECT, changed);
}

static void
update_permissions (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv = chan->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (chan);
  TpChannelGroupFlags grp_flags_add, grp_flags_rem;

  /*
   * Update group flags.
   */
  grp_flags_add = TP_CHANNEL_GROUP_FLAG_CAN_ADD |
                  TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD;
  grp_flags_rem = 0;

  if (priv->self_role == WOCKY_MUC_ROLE_MODERATOR)
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

  /* Update RoomConfig.CanUpdateConfiguration */

  /* The room configuration is part of the "room definition", so is defined by
   * the XEP to be editable only by owners. */
  if (priv->self_affil == WOCKY_MUC_AFFILIATION_OWNER)
    {
      gabble_room_config_set_can_update_configuration (priv->room_config, TRUE);
    }
  else
    {
      gabble_room_config_set_can_update_configuration (priv->room_config, FALSE);
    }

  gabble_room_config_emit_properties_changed (priv->room_config);

  if (priv->self_affil == WOCKY_MUC_AFFILIATION_OWNER)
    {
      /* request the configuration form purely to see if the description
       * is writable by us in this room. sigh. GO MUC!!! */
      GabbleConnection *conn = GABBLE_CONNECTION (
          tp_base_channel_get_connection (base));
      WockyStanza *stanza = wocky_stanza_build (
          WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_GET, NULL, priv->jid,
          '(', "query", ':', WOCKY_NS_MUC_OWNER, ')', NULL);

      conn_util_send_iq_async (conn, stanza, NULL, perms_config_form_reply_cb,
          g_object_ref (chan));
      g_object_unref (stanza);
    }
}



/* ************************************************************************* */
/* wocky MUC implementation */

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

      DEBUG ("password required to join; signalling");
      change_must_provide_password (gmuc, TRUE);
      g_object_set (gmuc, "state", MUC_STATE_AUTH, NULL);
    }
  else
    {
      GError *tp_error = NULL;

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
            if (handle_nick_conflict (gmuc, stanza, &tp_error))
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

static TpChannelGroupChangeReason
muc_status_codes_to_change_reason (guint codes)
{
  if ((codes & WOCKY_MUC_CODE_BANNED) != 0)
    return TP_CHANNEL_GROUP_CHANGE_REASON_BANNED;
  else if ((codes & ( WOCKY_MUC_CODE_KICKED
                    | WOCKY_MUC_CODE_KICKED_AFFILIATION
                    | WOCKY_MUC_CODE_KICKED_ROOM_PRIVATISED
                    | WOCKY_MUC_CODE_KICKED_SHUTDOWN
                    )) != 0)
    return TP_CHANNEL_GROUP_CHANGE_REASON_KICKED;
  else
    return TP_CHANNEL_GROUP_CHANGE_REASON_NONE;
}

/* connect to wocky-muc:SIG_PARTED, which we will receive when the MUC tells *
 * us that we have left the channel                                          */
static void
handle_parted (GObject *source,
    WockyStanza *stanza,
    guint codes,
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
  const char *jid = wocky_muc_jid (wmuc);

  DEBUG ("called with jid='%s'", jid);

  member = tp_handle_ensure (contact_repo, jid, NULL, NULL);

  if (priv->closing)
    {
      /* This was a timeout to ensure that leaving a room with a
       * non-responsive conference server still meant the channel
       * closed (eventually). */
      clear_leave_timer (gmuc);

      /* Close has been called, and we informed the MUC of our leaving
       * by sending a presence stanza of type='unavailable'. Now this
       * has been returned to us we know we've successfully left the
       * MUC, so we can finally close the channel here. */
      tp_base_channel_destroyed (TP_BASE_CHANNEL (gmuc));

      return;
    }

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

  reason = muc_status_codes_to_change_reason (codes);

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
    guint codes,
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

  reason = muc_status_codes_to_change_reason (codes);

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

  priv->self_role = wocky_muc_role (wmuc);
  priv->self_affil = wocky_muc_affiliation (wmuc);

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
    guint codes,
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
    guint codes,
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
  if (codes & WOCKY_MUC_CODE_NEW_ROOM)
    {
      WockyStanza *accept = wocky_stanza_build (
          WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
          NULL, gmuc->priv->jid,
            '(', "query", ':', WOCKY_NS_MUC_OWNER,
              '(', "x", ':', WOCKY_XMPP_NS_DATA,
                '@', "type", "submit",
              ')',
            ')',
          NULL);
      conn_util_send_iq_async (GABBLE_CONNECTION (base_conn), accept, NULL,
          room_created_submit_reply_cb, NULL);
      g_object_unref (accept);
    }

  g_object_set (gmuc, "state", MUC_STATE_JOINED, NULL);

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
    guint codes,
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
          case WOCKY_MUC_MSG_STATE_COMPOSING:
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

      tp_svc_channel_interface_chat_state_emit_chat_state_changed (gmuc,
          from, tp_msg_state);
    }

  if (subject != NULL)
    _gabble_muc_channel_handle_subject (gmuc, handle_type, from,
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
  GabbleMucChannelPrivate *priv = gmuc->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (gmuc);
  TpBaseConnection *conn = tp_base_channel_get_connection (base);
  gboolean from_member = (who != NULL);
  TpChannelTextSendError tp_err = TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN;
  TpDeliveryStatus ds = TP_DELIVERY_STATUS_DELIVERED;
  TpHandleRepoIface *repo = NULL;
  TpHandleType handle_type;
  TpHandle from = 0;
  const gchar *subject;

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

  /* FIXME: this is stupid. WockyMuc gives us the subject for non-errors, but
   * doesn't bother for errors.
   */
  subject = wocky_node_get_content_from_child (
      wocky_stanza_get_top_node (stanza), "subject");

  /* The server is under no obligation to echo the <subject> element back if it
   * sends us an error. Fortunately, it should preserve the id='' element so we
   * can check for that instead.
   */
  if (subject != NULL ||
      (priv->set_subject_stanza_id != NULL &&
       !tp_strdiff (xmpp_id, priv->set_subject_stanza_id)))
    _gabble_muc_channel_handle_subject (gmuc,
        handle_type, from, stamp, subject, stanza);

  tp_handle_unref (repo, from);
}

/* ************************************************************************* */
/**
 * _gabble_muc_channel_handle_subject: handle room subject updates
 */
void
_gabble_muc_channel_handle_subject (GabbleMucChannel *chan,
                                    TpHandleType handle_type,
                                    TpHandle sender,
                                    time_t timestamp,
                                    const gchar *subject,
                                    LmMessage *msg)
{
  GabbleMucChannelPrivate *priv;
  const gchar *actor;
  GError *error = NULL;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = chan->priv;

  if (wocky_stanza_extract_errors (msg, NULL, &error, NULL, NULL))
    {
      if (priv->set_subject_context != NULL)
        {
          GError *tp_error = NULL;

          gabble_set_tp_error_from_wocky (error, &tp_error);
          if (tp_str_empty (tp_error->message))
            g_prefix_error (&tp_error, "failed to change subject");

          return_from_set_subject (chan, tp_error);
          g_clear_error (&tp_error);

          /* Get the properties into a consistent state. */
          room_properties_update (chan);
        }

      g_clear_error (&error);
      return;
    }


  /* Channel.Interface.Subject properties */
  if (handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (
          tp_base_channel_get_connection (TP_BASE_CHANNEL (chan)),
          handle_type);

      actor = tp_handle_inspect (contact_handles, sender);
    }
  else
    {
      actor = "";
    }

  g_free (priv->subject);
  g_free (priv->subject_actor);
  priv->subject = g_strdup (subject);
  priv->subject_actor = g_strdup (actor);
  priv->subject_timestamp = timestamp;

  DEBUG ("Subject changed to '%s' by '%s' at %" G_GINT64_FORMAT "",
      subject, actor, timestamp);

  /* Emit signals */
  emit_subject_changed (chan);

  if (priv->set_subject_context != NULL)
    return_from_set_subject (chan, NULL);
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
  TpBaseChannel *base;
  TpBaseConnection *base_conn;
  TpMessage *message;
  TpHandle muc_self_handle;
  gboolean is_echo;
  gboolean is_error;
  gchar *tmp;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

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

  message = tp_cm_message_new (base_conn, 2);

  /* Header common to normal message and delivery-echo */
  if (msg_type != TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL)
    tp_message_set_uint32 (message, 0, "message-type", msg_type);

  if (timestamp != 0)
    tp_message_set_int64 (message, 0, "message-sent", timestamp);

  /* Body */
  tp_message_set_string (message, 1, "content-type", "text/plain");
  tp_message_set_string (message, 1, "content", text);

  if (is_error || is_echo)
    {
      /* Error reports and echos of our own messages are represented as
       * delivery reports.
       */

      TpMessage *delivery_report = tp_cm_message_new (base_conn, 1);
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
      tp_cm_message_set_sender (message, muc_self_handle);

      /* If we sent the message whose delivery has succeeded or failed, we
       * trust the id='' attribute. */
      if (id != NULL)
        tp_message_set_string (message, 0, "message-token", id);

      tp_cm_message_take_message (delivery_report, 0, "delivery-echo",
          message);

      tp_message_mixin_take_received (G_OBJECT (chan), delivery_report);
    }
  else
    {
      /* Messages from the MUC itself should have no sender. */
      if (sender_handle_type == TP_HANDLE_TYPE_CONTACT)
        tp_cm_message_set_sender (message, sender);

      if (timestamp != 0)
        tp_message_set_boolean (message, 0, "scrollback", TRUE);

      if (id != NULL)
        tp_message_set_string (message, 0, "message-token", id);

      tp_message_mixin_take_received (G_OBJECT (chan), message);
    }
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
      priv->must_provide_password ? TP_CHANNEL_PASSWORD_FLAG_PROVIDE : 0);
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
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (self));

  priv = self->priv;

  if (!priv->must_provide_password ||
      priv->password_ctx != NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "password cannot be provided in the current state" };
      dbus_g_method_return_error (context, &error);
    }
  else
    {
      g_object_set (priv->wmuc, "password", password, NULL);
      send_join_request (self);
      priv->password_ctx = context;
    }
}

static void
_gabble_muc_channel_message_sent_cb (GObject *source,
                                     GAsyncResult *res,
                                     gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source);
  _GabbleMUCSendMessageCtx *context = user_data;
  GabbleMucChannel *chan = context->channel;
  TpMessage *message = context->message;
  GError *error = NULL;

  if (wocky_porter_send_finish (porter, res, &error))
    {
      tp_message_mixin_sent ((GObject *) chan, message,
          TP_MESSAGE_SENDING_FLAG_REPORT_DELIVERY, context->token, NULL);
    }
  else
    {
      tp_message_mixin_sent ((GObject *) chan, context->message,
          TP_MESSAGE_SENDING_FLAG_REPORT_DELIVERY, NULL, error);
      g_free (error);
    }

  g_object_unref (context->channel);
  g_object_unref (context->message);
  g_free (context->token);
  g_slice_free (_GabbleMUCSendMessageCtx, context);
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
  GabbleConnection *gabble_conn =
      GABBLE_CONNECTION (tp_base_channel_get_connection (base));
  _GabbleMUCSendMessageCtx *context = NULL;
  WockyStanza *stanza = NULL;
  WockyPorter *porter = NULL;
  GError *error = NULL;
  gchar *id = NULL;

  stanza = gabble_message_util_build_stanza (message, gabble_conn,
      LM_MESSAGE_SUB_TYPE_GROUPCHAT, TP_CHANNEL_CHAT_STATE_ACTIVE,
      priv->jid, FALSE, &id, &error);

  if (stanza != NULL)
    {
      context = g_slice_new0 (_GabbleMUCSendMessageCtx);
      context->channel = g_object_ref (obj);
      context->message = g_object_ref (message);
      context->token = id;
      porter = gabble_connection_dup_porter (gabble_conn);
      wocky_porter_send_async (porter, stanza, NULL,
          _gabble_muc_channel_message_sent_cb, context);
      g_object_unref (stanza);
      g_object_unref (porter);
   }
  else
   {
     tp_message_mixin_sent (obj, message, flags, NULL, error);
     g_error_free (error);
   }
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
      send_join_request (self);
      g_object_set (obj, "state", MUC_STATE_INITIATED, NULL);

      /* deny adding */
      tp_group_mixin_change_flags (obj, 0, TP_CHANNEL_GROUP_FLAG_CAN_ADD);
      return TRUE;
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


static void request_config_form_reply_cb (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data);

static void
gabble_muc_channel_update_configuration_async (
    TpBaseChannel *base,
    GHashTable *validated_properties,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (base);
  GabbleMucChannelPrivate *priv = self->priv;
  GabbleConnection *conn =
      GABBLE_CONNECTION (tp_base_channel_get_connection (base));
  WockyStanza *stanza;
  GSimpleAsyncResult *result = g_simple_async_result_new ((GObject *) self,
      callback, user_data, gabble_muc_channel_update_configuration_async);

  g_assert (priv->properties_being_updated == NULL);

  stanza = wocky_stanza_build (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_GET,
      NULL, priv->jid,
      '(', "query", ':', WOCKY_NS_MUC_OWNER, ')', NULL);
  conn_util_send_iq_async (conn, stanza, NULL,
      request_config_form_reply_cb, result);
  g_object_unref (stanza);

  priv->properties_being_updated = validated_properties;
}

typedef const gchar * (*MapFieldFunc) (const GValue *value);

typedef struct {
    const gchar *var;
    GabbleRoomConfigProperty prop_id;
    MapFieldFunc map;
} ConfigFormMapping;

static const gchar *
map_bool (const GValue *value)
{
  return g_value_get_boolean (value) ? "1" : "0";
}

static const gchar *
map_bool_inverted (const GValue *value)
{
  return g_value_get_boolean (value) ? "0" : "1";
}

static const gchar *
map_roomconfig_whois (const GValue *value)
{
  return g_value_get_boolean (value) ? "moderators" : "anyone";
}

static const gchar *
map_owner_whois (const GValue *value)
{
  return g_value_get_boolean (value) ? "admins" : "anyone";
}

static ConfigFormMapping form_mappings[] = {
    { "anonymous", GABBLE_ROOM_CONFIG_ANONYMOUS, map_bool },
    { "muc#roomconfig_whois", GABBLE_ROOM_CONFIG_ANONYMOUS, map_roomconfig_whois },
    { "muc#owner_whois", GABBLE_ROOM_CONFIG_ANONYMOUS, map_owner_whois },

    { "members_only", GABBLE_ROOM_CONFIG_INVITE_ONLY, map_bool },
    { "muc#roomconfig_membersonly", GABBLE_ROOM_CONFIG_INVITE_ONLY, map_bool },
    { "muc#owner_inviteonly", GABBLE_ROOM_CONFIG_INVITE_ONLY, map_bool },

    { "moderated", GABBLE_ROOM_CONFIG_MODERATED, map_bool },
    { "muc#roomconfig_moderatedroom", GABBLE_ROOM_CONFIG_MODERATED, map_bool },
    { "muc#owner_moderatedroom", GABBLE_ROOM_CONFIG_MODERATED, map_bool },

    { "title", GABBLE_ROOM_CONFIG_TITLE, g_value_get_string },
    { "muc#roomconfig_roomname", GABBLE_ROOM_CONFIG_TITLE, g_value_get_string },
    { "muc#owner_roomname", GABBLE_ROOM_CONFIG_TITLE, g_value_get_string },

    { "muc#roomconfig_roomdesc", GABBLE_ROOM_CONFIG_DESCRIPTION, g_value_get_string },
    { "muc#owner_roomdesc", GABBLE_ROOM_CONFIG_DESCRIPTION, g_value_get_string },

    { "password", GABBLE_ROOM_CONFIG_PASSWORD, g_value_get_string },
    { "muc#roomconfig_roomsecret", GABBLE_ROOM_CONFIG_PASSWORD, g_value_get_string },
    { "muc#owner_roomsecret", GABBLE_ROOM_CONFIG_PASSWORD, g_value_get_string },

    { "password_protected", GABBLE_ROOM_CONFIG_PASSWORD_PROTECTED, map_bool },
    { "muc#roomconfig_passwordprotectedroom", GABBLE_ROOM_CONFIG_PASSWORD_PROTECTED, map_bool },
    { "muc#owner_passwordprotectedroom", GABBLE_ROOM_CONFIG_PASSWORD_PROTECTED, map_bool },

    { "persistent", GABBLE_ROOM_CONFIG_PERSISTENT, map_bool },
    { "muc#roomconfig_persistentroom", GABBLE_ROOM_CONFIG_PERSISTENT, map_bool },
    { "muc#owner_persistentroom", GABBLE_ROOM_CONFIG_PERSISTENT, map_bool },

    { "public", GABBLE_ROOM_CONFIG_PRIVATE, map_bool_inverted },
    { "muc#roomconfig_publicroom", GABBLE_ROOM_CONFIG_PRIVATE, map_bool_inverted },
    { "muc#owner_publicroom", GABBLE_ROOM_CONFIG_PRIVATE, map_bool_inverted },

    { NULL }
};

static ConfigFormMapping *
lookup_config_form_field (const gchar *var)
{
  ConfigFormMapping *f;

  for (f = form_mappings; f->var != NULL; f++)
    if (strcmp (var, f->var) == 0)
      return f;

  DEBUG ("unknown field %s", var);

  return NULL;
}

static void request_config_form_submit_reply_cb (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data);

static void
request_config_form_reply_cb (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (source);
  GSimpleAsyncResult *update_result = G_SIMPLE_ASYNC_RESULT (user_data);
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (
      g_async_result_get_source_object ((GAsyncResult *) update_result));
  GabbleMucChannelPrivate *priv = chan->priv;
  GHashTable *properties = priv->properties_being_updated;
  WockyStanza *reply = NULL;
  WockyStanza *submit_iq = NULL;
  WockyNode *form_node, *submit_node, *child;
  GError *error = NULL;
  guint i, props_left;
  WockyNodeIter j;

  if (!conn_util_send_iq_finish (conn, result, &reply, &error))
    {
      g_prefix_error (&error, "failed to request configuration form: ");
      goto OUT;
    }

  form_node = config_form_get_form_node (reply);
  if (form_node == NULL)
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_SERVICE_CONFUSED,
          "MUC configuration form didn't actually contain a form");
      goto OUT;
    }

  /* initialize */
  submit_iq = wocky_stanza_build (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
      NULL, priv->jid,
      '(',
        "query", ':', WOCKY_NS_MUC_OWNER,
        '(',
          "x", ':', WOCKY_XMPP_NS_DATA,
          '@', "type", "submit",
          '*', &submit_node,
        ')',
      ')', NULL);

  /* we assume that the number of props will fit in a guint on all supported
   * platforms, so fail at compile time if this is no longer the case
   */
#if GABBLE_NUM_ROOM_CONFIG_PROPERTIES > 32
#error GabbleMUCChannel request_config_form_reply_cb needs porting to TpIntSet
#endif

  props_left = 0;
  for (i = 0; i < GABBLE_NUM_ROOM_CONFIG_PROPERTIES; i++)
    {
      if (g_hash_table_lookup (properties, GUINT_TO_POINTER (i)) != NULL)
        props_left |= 1 << i;
    }

  wocky_node_iter_init (&j, form_node, "field", NULL);
  while (wocky_node_iter_next (&j, &child))
    {
      const gchar *var, *type_str;
      LmMessageNode *field_node;
      ConfigFormMapping *f;
      GValue *value = NULL;

      var = wocky_node_get_attribute (child, "var");
      if (var == NULL) {
        DEBUG ("skipping node '%s' because of lacking var attribute",
               child->name);
        continue;
      }

      f = lookup_config_form_field (var);

      /* add the corresponding field node to the reply message */
      field_node = wocky_node_add_child (submit_node, "field");
      wocky_node_set_attribute (field_node, "var", var);

      type_str = wocky_node_get_attribute (child, "type");
      if (type_str != NULL)
        {
          wocky_node_set_attribute (field_node, "type", type_str);
        }

      if (f != NULL)
        value = g_hash_table_lookup (properties, GUINT_TO_POINTER (f->prop_id));

      if (value != NULL)
        {
          const gchar *val_str;

          /* Known property and we have a value to set */
          DEBUG ("transforming %s...",
              wocky_enum_to_nick (GABBLE_TYPE_ROOM_CONFIG_PROPERTY,
                  f->prop_id));
          g_assert (f->map != NULL);
          val_str = f->map (value);

          /* add the corresponding value node(s) to the reply message */
          DEBUG ("Setting value %s for %s", val_str, var);
          wocky_node_add_child_with_content (field_node, "value", val_str);

          props_left &= ~(1 << f->prop_id);
        }
      else
        {
          /* Copy all the <value> nodes */
          WockyNodeIter k;
          WockyNode *value_node;

          wocky_node_iter_init (&k, child, "value", NULL);
          while (wocky_node_iter_next (&k, &value_node))
            wocky_node_add_child_with_content (field_node, "value",
                value_node->content);
        }
    }

  if (props_left != 0)
    {
      GString *unsubstituted = g_string_new ("");

      printf (TP_ANSI_BOLD_ON TP_ANSI_FG_WHITE TP_ANSI_BG_RED
              "\n%s: the following properties were not substituted:\n",
              G_STRFUNC);

      for (i = 0; i < GABBLE_NUM_ROOM_CONFIG_PROPERTIES; i++)
        {
          if ((props_left & (1 << i)) != 0)
            {
              const gchar *name = wocky_enum_to_nick (
                  GABBLE_TYPE_ROOM_CONFIG_PROPERTY, i);
              printf ("  %s\n", name);

              if (unsubstituted->len > 0)
                g_string_append (unsubstituted, ", ");

              g_string_append (unsubstituted, name);
            }
        }

      printf ("\nthis is a MUC server compatibility bug in gabble, please "
              "report it with a full debug log attached (running gabble "
              "with WOCKY_DEBUG=xmpp)" TP_ANSI_RESET "\n\n");
      fflush (stdout);

      error = g_error_new (TP_ERRORS, TP_ERROR_SERVICE_CONFUSED,
          "Couldn't find fields corresponding to %s in the muc#owner form. "
          "This is a MUC server compatibility bug in Gabble.",
          unsubstituted->str);
      g_string_free (unsubstituted, TRUE);
      goto OUT;
    }

  conn_util_send_iq_async (conn, submit_iq, NULL,
      request_config_form_submit_reply_cb, update_result);

OUT:
  if (error != NULL)
    {
      g_simple_async_result_set_from_error (update_result, error);
      g_simple_async_result_complete (update_result);
      g_object_unref (update_result);
      priv->properties_being_updated = NULL;
      g_clear_error (&error);
    }

  tp_clear_object (&reply);
  tp_clear_object (&submit_iq);
  g_object_unref (chan);
}

static void
request_config_form_submit_reply_cb (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GSimpleAsyncResult *update_result = G_SIMPLE_ASYNC_RESULT (user_data);
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (
      g_async_result_get_source_object ((GAsyncResult *) update_result));
  GabbleMucChannelPrivate *priv = chan->priv;
  GError *error = NULL;

  if (!conn_util_send_iq_finish (GABBLE_CONNECTION (source), result, NULL, &error))
    {
      g_prefix_error (&error, "submitted configuration form was rejected: ");
      g_simple_async_result_set_from_error (update_result, error);
      g_clear_error (&error);
    }

  g_simple_async_result_complete (update_result);
  priv->properties_being_updated = NULL;

  /* Get the properties into a consistent state. */
  room_properties_update (chan);

  g_object_unref (chan);
  g_object_unref (update_result);
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

void
gabble_muc_channel_send_presence (GabbleMucChannel *self)
{
  GabbleMucChannelPrivate *priv = self->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  WockyStanza *stanza;

  /* do nothing if we havn't actually joined yet */
  if (priv->state < MUC_STATE_INITIATED)
    return;

  stanza = wocky_muc_create_presence (priv->wmuc,
      WOCKY_STANZA_SUB_TYPE_NONE, NULL);
  _gabble_connection_send (
      GABBLE_CONNECTION (tp_base_channel_get_connection (base)),
      stanza, NULL);
  g_object_unref (stanza);
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
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  const gchar *prefix;

  g_assert (!priv->call_initiating);
  g_assert (priv->call == NULL);

  priv->call_initiating = TRUE;

  /* We want to put the call channel "under" ourself, but let it decide its
   * exact path. TpBaseChannel makes this a little awkward — the vfunc for
   * picking your own path is supposed to return the suffix, and the base class
   * pastes on the connection's path before that.
   *
   * So... we pass the bit of our own path that's after the connection's path
   * to the call channel; it builds its suffix based on that and its own
   * address; and finally TpBaseChannel pastes the connection path back on. :)
   */
  prefix = tp_base_channel_get_object_path (base) +
      strlen (base_conn->object_path) + 1 /* for the slash */;

  /* Keep ourselves reffed while call channels are created */
  g_object_ref (gmuc);
  gabble_call_muc_channel_new_async (
      GABBLE_CONNECTION (base_conn),
      priv->requests_cancellable,
      prefix,
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
sent_subject_cb (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (user_data);
  GabbleMucChannelPrivate *priv = self->priv;
  GError *error = NULL;

  if (!wocky_porter_send_finish (WOCKY_PORTER (source), result, &error))
    {
      DEBUG ("buh, failed to send a <message> to change the subject: %s",
          error->message);

      if (priv->set_subject_context != NULL)
        {
          GError *tp_error = NULL;

          gabble_set_tp_error_from_wocky (error, &tp_error);
          return_from_set_subject (self, tp_error);
          g_clear_error (&tp_error);
        }

      g_clear_error (&error);
    }
  /* otherwise, we wait for a reply! */

  g_object_unref (self);
}

static void
gabble_muc_channel_set_subject (TpSvcChannelInterfaceSubject *iface,
    const gchar *subject,
    DBusGMethodInvocation *context)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (iface);
  GabbleMucChannelPrivate *priv = self->priv;
  GabbleConnection *conn = GABBLE_CONNECTION (tp_base_channel_get_connection (
          TP_BASE_CHANNEL (self)));
  WockyPorter *porter = wocky_session_get_porter (conn->session);

  if (priv->set_subject_context != NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Hey! Stop changing the subject! (Your last request is still in "
          "flight.)" };

      dbus_g_method_return_error (context, &error);
    }
  else
    {
      WockyXmppConnection *xmpp_conn;
      WockyStanza *stanza;

      g_assert (priv->set_subject_stanza_id == NULL);
      g_object_get (porter,
          "connection", &xmpp_conn,
          NULL);
      priv->set_subject_stanza_id = wocky_xmpp_connection_new_id (xmpp_conn);
      g_object_unref (xmpp_conn);

      stanza = wocky_stanza_build (
          WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_GROUPCHAT,
          NULL, priv->jid,
          '@', "id", priv->set_subject_stanza_id,
          '(', "subject", '$', subject, ')',
          NULL);

      priv->set_subject_context = context;
      wocky_porter_send_async (porter, stanza, NULL, sent_subject_cb,
          g_object_ref (self));
      g_object_unref (stanza);
    }
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

static void
subject_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceSubjectClass *klass =
    (TpSvcChannelInterfaceSubjectClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_subject_implement_##x (\
    klass, gabble_muc_channel_##x)
  IMPLEMENT(set_subject);
#undef IMPLEMENT
}
