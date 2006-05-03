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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "allocator.h"
#include "gabble-connection.h"
#include "gabble-disco.h"

#include "telepathy-errors.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"

#include "gabble-muc-channel.h"
#include "gabble-muc-channel-signals-marshal.h"

#include "gabble-muc-channel-glue.h"

#define MAX_PENDING_MESSAGES 256
#define MAX_MESSAGE_SIZE 8*1024 - 1

G_DEFINE_TYPE(GabbleMucChannel, gabble_muc_channel, G_TYPE_OBJECT)

#define DEFAULT_JOIN_TIMEOUT (180 * 1000)

/*
 * FIXME: move these and the other defines in gabble-media-session.h
 *        to a common header
 */
#define ANSI_RESET      "\x1b[0m"
#define ANSI_BOLD_ON    "\x1b[1m"
#define ANSI_BOLD_OFF   "\x1b[22m"
#define ANSI_FG_CYAN    "\x1b[36m"
#define ANSI_FG_WHITE   "\x1b[37m"
#define ANSI_BG_RED     "\x1b[41m"

/* signal enum */
enum
{
    CLOSED,
    PASSWORD_FLAGS_CHANGED,
    PROPERTIES_CHANGED,
    PROPERTY_FLAGS_CHANGED,
    RECEIVED,
    SENT,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE,
  PROP_STATE,
  LAST_PROPERTY
};

typedef enum {
    MUC_STATE_CREATED = 0,
    MUC_STATE_INITIATED,
    MUC_STATE_AUTH,
    MUC_STATE_JOINED,
    MUC_STATE_ENDED,
} GabbleMucState;

static const gchar *muc_states[] =
{
  "MUC_STATE_CREATED",
  "MUC_STATE_INITIATED",
  "MUC_STATE_AUTH",
  "MUC_STATE_JOINED",
  "MUC_STATE_ENDED",
};

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

static const gchar *muc_roles[NUM_ROLES] =
{
  "none",
  "visitor",
  "participant",
  "moderator",
};

static const gchar *muc_affiliations[NUM_AFFILIATIONS] =
{
  "none",
  "member",
  "admin",
  "owner",
};

/* room properties */
enum
{
  ROOM_PROP_ANONYMOUS = 0,
  ROOM_PROP_INVITE_ONLY,
  ROOM_PROP_MODERATED,
  ROOM_PROP_NAME,
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

struct _RoomPropertySignature {
    gchar *name;
    GType type;
};

typedef struct _RoomPropertySignature RoomPropertySignature;

const RoomPropertySignature room_property_signatures[NUM_ROOM_PROPS] = {
      { "anonymous",         G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "invite-only",       G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "moderated",         G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "name",              G_TYPE_STRING },   /* impl: READ, WRITE */
      { "password",          G_TYPE_STRING },   /* impl: WRITE */
      { "password-required", G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "persistent",        G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "private",           G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "subject",           G_TYPE_STRING },   /* impl: READ, WRITE */
      { "subject-contact",   G_TYPE_UINT },     /* impl: READ */
      { "subject-timestamp", G_TYPE_UINT },     /* impl: READ */
};

struct _RoomProperty {
    GValue value;
    guint flags;
};

typedef struct _RoomProperty RoomProperty;

/* private structures */

typedef struct {
    DBusGMethodInvocation *call_ctx;
    guint32 remaining;
    GValue *values[NUM_ROOM_PROPS];
} SetPropertiesContext;

typedef struct _GabbleMucChannelPrivate GabbleMucChannelPrivate;

struct _GabbleMucChannelPrivate
{
  GabbleConnection *conn;
  gchar *object_path;

  GabbleMucState state;

  guint join_timer_id;

  TpChannelPasswordFlags password_flags;
  DBusGMethodInvocation *password_ctx;

  GabbleHandle handle;
  const gchar *jid;

  gchar *self_jid;
  GabbleMucRole self_role;
  GabbleMucAffiliation self_affil;

  guint recv_id;
  GQueue *pending_messages;

  RoomProperty room_props[NUM_ROOM_PROPS];
  SetPropertiesContext set_props_ctx;

  gboolean closed;
  gboolean dispose_has_run;
};

/* pending message */
typedef struct _GabbleMucPendingMessage GabbleMucPendingMessage;

struct _GabbleMucPendingMessage
{
  guint id;
  time_t timestamp;
  GabbleHandle sender;
  TpChannelTextMessageType type;
  gchar *text;
};

#define GABBLE_MUC_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MUC_CHANNEL, GabbleMucChannelPrivate))

static void
gabble_muc_channel_init (GabbleMucChannel *obj)
{
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  priv->pending_messages = g_queue_new ();
}

static void contact_handle_to_room_identity (GabbleMucChannel *chan, GabbleHandle main_handle, GabbleHandle *room_handle, gchar **room_jid);
static void room_properties_init (GabbleMucChannel *chan);

static GObject *
gabble_muc_channel_constructor (GType type, guint n_props,
                                GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMucChannelPrivate *priv;
  DBusGConnection *bus;
  GabbleHandleRepo *handles;
  GabbleHandle self_handle;
  gboolean valid;

  obj = G_OBJECT_CLASS (gabble_muc_channel_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  handles = priv->conn->handles;

  /* ref our room handle */
  valid = gabble_handle_ref (handles, TP_HANDLE_TYPE_ROOM, priv->handle);
  g_assert (valid);

  /* get the room's jid */
  priv->jid = gabble_handle_inspect (handles, TP_HANDLE_TYPE_ROOM, priv->handle);

  /* get our own identity in the room */
  contact_handle_to_room_identity (GABBLE_MUC_CHANNEL (obj), priv->conn->self_handle,
                                   &self_handle, &priv->self_jid);

  /* initialize our own role and affiliation */
  priv->self_role = ROLE_NONE;
  priv->self_affil = AFFILIATION_NONE;

  /* register object on the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  /* initialize group mixin */
  gabble_group_mixin_init (obj, G_STRUCT_OFFSET (GabbleMucChannel, group),
                           handles, self_handle);

  /* allow adding ourself */
  gabble_group_mixin_change_flags (obj, TP_CHANNEL_GROUP_FLAG_CAN_ADD, 0);

  /* initialize room properties */
  room_properties_init (GABBLE_MUC_CHANNEL (obj));

  return obj;
}

static void
room_properties_init (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv;
  guint i;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  for (i = 0; i < NUM_ROOM_PROPS; i++)
    {
      RoomProperty *prop = &priv->room_props[i];

      g_value_init (&prop->value, room_property_signatures[i].type);
      prop->flags = 0;
    }
}

static void room_property_change_value (GabbleMucChannel *chan, guint prop_id, const GValue *new_value, GArray *props);
static void room_property_change_flags (GabbleMucChannel *chan, guint prop_id, TpPropertyFlags add, TpPropertyFlags remove, GArray *props);
static void room_properties_emit_changed (GabbleMucChannel *chan, GArray *props);
static void room_properties_emit_flags (GabbleMucChannel *chan, GArray *props);

static void properties_disco_cb (GabbleDisco *disco, const gchar *jid,
                                 const gchar *node, LmMessageNode *query_result,
                                 GError *error, gpointer user_data)
{
  GabbleMucChannel *chan = user_data;
  GabbleMucChannelPrivate *priv;
  GArray *changed_props_val, *changed_props_flags;
  LmMessageNode *lm_node;
  const gchar *str;
  GValue val = { 0, };

  HANDLER_DEBUG (query_result, "disco query result");

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  if (error != NULL)
    {
      HANDLER_DEBUG (query_result, "disco query failed");
      return;
    }

  changed_props_val = g_array_sized_new (FALSE, FALSE, sizeof (guint),
                                         NUM_ROOM_PROPS);
  changed_props_flags = g_array_sized_new (FALSE, FALSE, sizeof (guint),
                                           NUM_ROOM_PROPS);


  /*
   * Update room definition.
   */

  /* ROOM_PROP_NAME */
  lm_node = lm_message_node_get_child (query_result, "identity");
  if (lm_node)
    {
      str = lm_message_node_get_attribute (lm_node, "type");
      g_assert (str && strcmp (str, "text") == 0);

      str = lm_message_node_get_attribute (lm_node, "category");
      g_assert (str && strcmp (str, "conference") == 0);

      str = lm_message_node_get_attribute (lm_node, "name");
      if (str)
        {
          g_value_init (&val, G_TYPE_STRING);
          g_value_set_string (&val, str);

          room_property_change_value (chan, ROOM_PROP_NAME, &val,
                                      changed_props_val);

          room_property_change_flags (chan, ROOM_PROP_NAME,
                                      TP_PROPERTY_FLAG_READ,
                                      0, changed_props_flags);

          g_value_unset (&val);
        }
    }

  for (lm_node = query_result->children; lm_node; lm_node = lm_node->next)
    {
      guint prop_id = INVALID_ROOM_PROP;

      if (strcmp (lm_node->name, "feature") != 0)
        continue;

      str = lm_message_node_get_attribute (lm_node, "var");
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
      else if (strcmp (str, "http://jabber.org/protocol/muc") == 0)
        {
        }

      /* Unhandled */
      else
        {
          g_warning ("%s: unhandled feature '%s'", G_STRFUNC, str);
        }

      if (prop_id != INVALID_ROOM_PROP)
        {
          room_property_change_value (chan, prop_id, &val, changed_props_val);

          room_property_change_flags (chan, prop_id, TP_PROPERTY_FLAG_READ,
                                      0, changed_props_flags);

          g_value_unset (&val);
        }
    }


  /*
   * Update write capabilities based on room configuration
   * and own role and affiliation.
   */

  /* Subject */
  /* FIXME: this might be allowed for participants/moderators only,
   *        so for now just rely on the server making that call. */
  if (priv->self_role >= ROLE_VISITOR)
    {
      room_property_change_flags (chan, ROOM_PROP_SUBJECT,
          TP_PROPERTY_FLAG_WRITE, 0, changed_props_flags);
    }

  /* Room definition */
  if (priv->self_affil == AFFILIATION_OWNER)
    {
      room_property_change_flags (chan, ROOM_PROP_ANONYMOUS,
          TP_PROPERTY_FLAG_WRITE, 0, changed_props_flags);

      room_property_change_flags (chan, ROOM_PROP_INVITE_ONLY,
          TP_PROPERTY_FLAG_WRITE, 0, changed_props_flags);

      room_property_change_flags (chan, ROOM_PROP_MODERATED,
          TP_PROPERTY_FLAG_WRITE, 0, changed_props_flags);

      room_property_change_flags (chan, ROOM_PROP_NAME,
          TP_PROPERTY_FLAG_WRITE, 0, changed_props_flags);

      room_property_change_flags (chan, ROOM_PROP_PASSWORD,
          TP_PROPERTY_FLAG_WRITE, 0, changed_props_flags);

      room_property_change_flags (chan, ROOM_PROP_PASSWORD_REQUIRED,
          TP_PROPERTY_FLAG_WRITE, 0, changed_props_flags);

      room_property_change_flags (chan, ROOM_PROP_PERSISTENT,
          TP_PROPERTY_FLAG_WRITE, 0, changed_props_flags);

      room_property_change_flags (chan, ROOM_PROP_PRIVATE,
          TP_PROPERTY_FLAG_WRITE, 0, changed_props_flags);

      room_property_change_flags (chan, ROOM_PROP_SUBJECT,
          TP_PROPERTY_FLAG_WRITE, 0, changed_props_flags);
    }


  /*
   * Emit signals.
   */
  room_properties_emit_changed (chan, changed_props_val);
  room_properties_emit_flags (chan, changed_props_flags);

  g_array_free (changed_props_val, TRUE);
  g_array_free (changed_props_flags, TRUE);
}

static void
room_properties_update (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv;
  GError *error;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  if (gabble_disco_request (priv->conn->disco, GABBLE_DISCO_TYPE_INFO, priv->jid, NULL,
        properties_disco_cb, chan, G_OBJECT (chan), &error) == NULL)
    {
      g_warning ("%s: disco query failed: '%s'", G_STRFUNC, error->message);
      return;
    }
}

static void
contact_handle_to_room_identity (GabbleMucChannel *chan, GabbleHandle main_handle,
                                 GabbleHandle *room_handle, gchar **room_jid)
{
  GabbleMucChannelPrivate *priv;
  GabbleHandleRepo *handles;
  const gchar *main_jid;
  gchar *username, *server;
  gchar *jid;

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  handles = priv->conn->handles;

  main_jid = gabble_handle_inspect (handles, TP_HANDLE_TYPE_CONTACT,
                                    main_handle);

  gabble_handle_decode_jid (main_jid, &username, &server, NULL);

  jid = g_strdup_printf ("%s/%s", priv->jid, username);

  g_free (username);
  g_free (server);

  if (room_handle)
    {
      *room_handle = gabble_handle_for_contact (handles, jid, TRUE);
    }

  if (room_jid)
    {
      *room_jid = jid;
    }
  else
    {
      g_free (jid);
    }
}

static gboolean
send_join_request (GabbleMucChannel *channel,
                   const gchar *password,
                   GError **error)
{
  GabbleMucChannelPrivate *priv;
  LmMessage *msg;
  LmMessageNode *x_node;
  gboolean ret;

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (channel);

  /* build the message */
  msg = lm_message_new (priv->self_jid, LM_MESSAGE_TYPE_PRESENCE);

  x_node = lm_message_node_add_child (msg->node, "x", NULL);
  lm_message_node_set_attribute (x_node, "xmlns", "http://jabber.org/protocol/muc");

  if (password != NULL)
    {
      lm_message_node_add_child (x_node, "password", password);
    }

  /* send it */
  ret = _gabble_connection_send (priv->conn, msg, error);
  if (!ret)
    {
      g_warning ("%s: _gabble_connection_send_with_reply failed", G_STRFUNC);
    }
  else
    {
      g_debug ("%s: join request sent", G_STRFUNC);
    }

  lm_message_unref (msg);

  return ret;
}

static gboolean
send_leave_message (GabbleMucChannel *channel,
                    const gchar *reason)
{
  GabbleMucChannelPrivate *priv;
  LmMessage *msg;
  GError *error;
  gboolean ret;

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (channel);

  /* build the message */
  msg = lm_message_new_with_sub_type (priv->self_jid, LM_MESSAGE_TYPE_PRESENCE,
                                      LM_MESSAGE_SUB_TYPE_UNAVAILABLE);

  if (reason != NULL)
    {
      lm_message_node_add_child (msg->node, "status", reason);
    }

  /* send it */
  ret = _gabble_connection_send (priv->conn, msg, &error);
  if (!ret)
    {
      g_warning ("%s: _gabble_connection_send_with_reply failed", G_STRFUNC);
      g_error_free (error);
    }
  else
    {
      g_debug ("%s: leave message sent", G_STRFUNC);
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
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
    case PROP_STATE:
      g_value_set_uint (value, priv->state);
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
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);
  GabbleMucState prev_state;

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      priv->handle = g_value_get_uint (value);
      break;
    case PROP_STATE:
      prev_state = priv->state;
      priv->state = g_value_get_uint (value);

      if (priv->state != prev_state)
        channel_state_changed (chan, prev_state, priv->state);

      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_muc_channel_dispose (GObject *object);
static void gabble_muc_channel_finalize (GObject *object);
static gboolean gabble_muc_channel_add_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error);
static gboolean gabble_muc_channel_remove_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error);

static void
gabble_muc_channel_class_init (GabbleMucChannelClass *gabble_muc_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_muc_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_muc_channel_class, sizeof (GabbleMucChannelPrivate));

  object_class->constructor = gabble_muc_channel_constructor;

  object_class->get_property = gabble_muc_channel_get_property;
  object_class->set_property = gabble_muc_channel_set_property;

  object_class->dispose = gabble_muc_channel_dispose;
  object_class->finalize = gabble_muc_channel_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "MUC channel object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                    "The D-Bus object path used for this "
                                    "object on the bus.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_string ("channel-type", "Telepathy channel type",
                                    "The D-Bus interface representing the "
                                    "type of this channel.",
                                    NULL,
                                    G_PARAM_READABLE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CHANNEL_TYPE, param_spec);

  param_spec = g_param_spec_uint ("handle", "Room handle",
                                  "The GabbleHandle representing the room "
                                  "with whom this channel communicates.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE, param_spec);

  param_spec = g_param_spec_uint ("state", "Channel state",
                                  "The current state that the channel is in.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[PASSWORD_FLAGS_CHANGED] =
    g_signal_new ("password-flags-changed",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[PROPERTIES_CHANGED] =
    g_signal_new ("properties-changed",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_VALUE, G_TYPE_INVALID)))));

  signals[PROPERTY_FLAGS_CHANGED] =
    g_signal_new ("property-flags-changed",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID)))));

  signals[RECEIVED] =
    g_signal_new ("received",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__INT_INT_INT_INT_STRING,
                  G_TYPE_NONE, 5, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SENT] =
    g_signal_new ("sent",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__INT_INT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  gabble_group_mixin_class_init (object_class,
                                 G_STRUCT_OFFSET (GabbleMucChannelClass, group_class),
                                 gabble_muc_channel_add_member,
                                 gabble_muc_channel_remove_member);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_muc_channel_class), &dbus_glib_gabble_muc_channel_object_info);
}

static void clear_join_timer (GabbleMucChannel *chan);

void
gabble_muc_channel_dispose (GObject *object)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  g_debug (G_STRFUNC);

  priv->dispose_has_run = TRUE;

  clear_join_timer (self);

  if (G_OBJECT_CLASS (gabble_muc_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_muc_channel_parent_class)->dispose (object);
}

static void clear_message_queue (GabbleMucChannel *chan);

void
gabble_muc_channel_finalize (GObject *object)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);
  GabbleHandleRepo *handles = priv->conn->handles;

  g_debug (G_STRFUNC);

  /* free any data held directly by the object here */
  gabble_handle_unref (handles, TP_HANDLE_TYPE_ROOM, priv->handle);

  g_free (priv->object_path);
  g_free (priv->self_jid);

  clear_message_queue (self);

  g_queue_free (priv->pending_messages);

  gabble_group_mixin_finalize (object);

  G_OBJECT_CLASS (gabble_muc_channel_parent_class)->finalize (object);
}

static void clear_join_timer (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  if (priv->join_timer_id != 0)
    {
      g_source_remove (priv->join_timer_id);
      priv->join_timer_id = 0;
    }
}

static void
change_password_flags (GabbleMucChannel *chan,
                       TpChannelPasswordFlags add,
                       TpChannelPasswordFlags remove)
{
  GabbleMucChannelPrivate *priv;
  TpChannelGroupFlags added, removed;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  added = add & ~priv->password_flags;
  priv->password_flags |= added;

  removed = remove & priv->password_flags;
  priv->password_flags &= ~removed;

  if (add != 0 || remove != 0)
    {
      g_debug ("%s: emitting password flags changed, added 0x%X, removed 0x%X",
               G_STRFUNC, added, removed);

      g_signal_emit (chan, signals[PASSWORD_FLAGS_CHANGED], 0, added, removed);
    }
}

static void provide_password_return_if_pending (GabbleMucChannel *chan, gboolean success)
{
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

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

static void close_channel (GabbleMucChannel *chan, const gchar *reason, gboolean inform_muc);

static gboolean
timeout_join (gpointer data)
{
  GabbleMucChannel *chan = data;

  g_debug ("%s: join timed out, closing channel", G_STRFUNC);

  provide_password_return_if_pending (chan, FALSE);

  close_channel (chan, NULL, FALSE);

  return FALSE;
}

static void
channel_state_changed (GabbleMucChannel *chan,
                       GabbleMucState prev_state,
                       GabbleMucState new_state)
{
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  g_debug ("%s: state changed from %s to %s", G_STRFUNC,
           muc_states[prev_state], muc_states[new_state]);

  if (new_state == MUC_STATE_INITIATED)
    {
      priv->join_timer_id =
        g_timeout_add (DEFAULT_JOIN_TIMEOUT, timeout_join, chan);
    }
  else if (new_state == MUC_STATE_JOINED)
    {
      provide_password_return_if_pending (chan, TRUE);

      clear_join_timer (chan);
    }
}

/**
 * _gabble_muc_pending_get_alloc
 *
 * Returns a GabbleAllocator for creating up to 256 pending messages, but no
 * more.
 */
static GabbleAllocator *
_gabble_muc_pending_get_alloc ()
{
  static GabbleAllocator *alloc = NULL;

  if (alloc == NULL)
    alloc = gabble_allocator_new (sizeof(GabbleMucPendingMessage), MAX_PENDING_MESSAGES);

  return alloc;
}

#define _gabble_muc_pending_new() \
  (ga_new (_gabble_muc_pending_get_alloc (), GabbleMucPendingMessage))
#define _gabble_muc_pending_new0() \
  (ga_new0 (_gabble_muc_pending_get_alloc (), GabbleMucPendingMessage))

/**
 * _gabble_muc_pending_free
 *
 * Free up a GabbleMucPendingMessage struct.
 */
static void _gabble_muc_pending_free (GabbleMucPendingMessage *msg)
{
  g_free (msg->text);
  gabble_allocator_free (_gabble_muc_pending_get_alloc (), msg);
}

static void
close_channel (GabbleMucChannel *chan, const gchar *reason,
               gboolean inform_muc)
{
  GabbleMucChannelPrivate *priv;
  GIntSet *empty, *set;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  if (priv->closed)
    return;

  priv->closed = TRUE;

  /* Remove us from member list */
  empty = g_intset_new ();
  set = g_intset_new ();
  g_intset_add (set, GABBLE_GROUP_MIXIN (chan)->self_handle);

  gabble_group_mixin_change_members (G_OBJECT (chan),
                                     (reason != NULL) ? reason : "",
                                     empty, set, empty, empty);

  g_intset_destroy (empty);
  g_intset_destroy (set);

  /* Inform the MUC if requested */
  if (inform_muc && priv->state >= MUC_STATE_INITIATED)
    {
      send_leave_message (chan, reason);
    }

  /* Update state and emit Closed signal */
  g_object_set (chan, "state", MUC_STATE_ENDED, NULL);

  g_signal_emit (chan, signals[CLOSED], 0);
}

/**
 * _gabble_muc_channel_presence_error
 */
void
_gabble_muc_channel_presence_error (GabbleMucChannel *chan,
                                    const gchar *jid,
                                    LmMessageNode *pres_node)
{
  GabbleMucChannelPrivate *priv;
  LmMessageNode *error_node, *text_node;
  const gchar *code_str, *type, *text;
  gint code;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  if (strcmp (jid, priv->self_jid) != 0)
    {
      g_warning ("%s: presence error from other jids than self not handled",
                 G_STRFUNC);
      return;
    }

  error_node = lm_message_node_get_child (pres_node, "error");
  if (error_node == NULL)
    {
      g_warning ("%s: missing required node 'error'", G_STRFUNC);
      return;
    }

  text_node = lm_message_node_get_child (error_node, "text");
  if (text_node)
    {
      text = lm_message_node_get_value (text_node);
    }
  else
    {
      text = NULL;
    }

  code_str = lm_message_node_get_attribute (error_node, "code");
  type = lm_message_node_get_attribute (error_node, "type");

  if (code_str == NULL || type == NULL)
    {
      g_warning ("%s: missing required attribute", G_STRFUNC);
      HANDLER_DEBUG (pres_node, "presence node");
      return;
    }

  code = atoi (code_str);

  if (priv->state >= MUC_STATE_JOINED)
    {
      g_warning ("%s: presence error while already member of the channel -- NYI",
                 G_STRFUNC);
      return;
    }

  HANDLER_DEBUG (pres_node, "presence node");

  /* We're not a member, find out why the join request failed
   * and act accordingly. */
  switch (code) {
    case 401:
      /* channel can sit requiring a password indefinitely */
      clear_join_timer (chan);

      /* Password already provided and incorrect? */
      if (priv->state == MUC_STATE_AUTH)
        {
          provide_password_return_if_pending (chan, FALSE);

          return;
        }

      g_debug ("%s: password required to join, changing password flags",
               G_STRFUNC);

      change_password_flags (chan, TP_CHANNEL_PASSWORD_FLAG_PROVIDE, 0);

      g_object_set (chan, "state", MUC_STATE_AUTH, NULL);

      break;

    default:
      g_warning ("%s: unhandled errorcode %d", G_STRFUNC, code);
  }
}

static GabbleMucRole
get_role_from_string (const gchar *role)
{
  guint i;

  if (role == NULL)
    {
      return ROLE_VISITOR;
    }

  for (i = 0; i < NUM_ROLES; i++)
    {
      if (strcmp (role, muc_roles[i]) == 0)
        {
          return i;
        }
    }

  g_warning ("%s: unknown role '%s' -- defaulting to ROLE_VISITOR",
             G_STRFUNC, role);

  return ROLE_VISITOR;
}

static GabbleMucAffiliation
get_affiliation_from_string (const gchar *affil)
{
  guint i;

  if (affil == NULL)
    {
      return AFFILIATION_NONE;
    }

  for (i = 0; i < NUM_AFFILIATIONS; i++)
    {
      if (strcmp (affil, muc_affiliations[i]) == 0)
        {
          return i;
        }
    }

  g_warning ("%s: unknown affiliation '%s' -- defaulting to "
             "AFFILIATION_NONE", G_STRFUNC, affil);

  return AFFILIATION_NONE;
}

static LmHandlerResult
room_created_submit_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                              LmMessage *reply_msg, GObject *object,
                              gpointer user_data)
{
  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      g_warning ("%s: failed to submit room config", G_STRFUNC);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/**
 * _gabble_muc_channel_member_presence_updated
 */
void
_gabble_muc_channel_member_presence_updated (GabbleMucChannel *chan,
                                             GabbleHandle handle,
                                             LmMessage *message,
                                             LmMessageNode *x_node)
{
  GabbleMucChannelPrivate *priv;
  GIntSet *empty, *set;
  GabbleGroupMixin *mixin;
  LmMessageNode *item_node, *node;
  const gchar *affil, *role, *status_code;

  g_debug (G_STRFUNC);

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  mixin = GABBLE_GROUP_MIXIN (chan);

  item_node = lm_message_node_get_child (x_node, "item");
  if (item_node == NULL)
    {
      g_warning ("%s: node missing 'item' child, ignoring", G_STRFUNC);
      return;
    }

  node = lm_message_node_get_child (x_node, "status");
  if (node)
    {
      status_code = lm_message_node_get_attribute (node, "code");
    }
  else
    {
      status_code = NULL;
    }

  role = lm_message_node_get_attribute (item_node, "role");
  affil = lm_message_node_get_attribute (item_node, "affiliation");

  /* update channel members according to presence */
  empty = g_intset_new ();
  set = g_intset_new ();
  g_intset_add (set, handle);

  if (lm_message_get_sub_type (message) != LM_MESSAGE_SUB_TYPE_UNAVAILABLE)
    {
      if (!handle_set_is_member (mixin->members, handle))
        {
          gabble_group_mixin_change_members (G_OBJECT (chan), "", set, empty,
                                             empty, empty);

          if (handle == mixin->self_handle)
            {
              g_object_set (chan, "state", MUC_STATE_JOINED, NULL);
            }
        }

      if (handle == mixin->self_handle)
        {
          TpChannelGroupFlags flags_add, flags_rem;

          priv->self_role = get_role_from_string (role);
          priv->self_affil = get_affiliation_from_string (affil);

          /* update flags */
          flags_add = TP_CHANNEL_GROUP_FLAG_CAN_ADD |
                      TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD;
          flags_rem = 0;

          if (priv->self_role == ROLE_MODERATOR)
            {
              flags_add |= TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
                           TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE;
            }
          else
            {
              flags_rem |= TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
                           TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE;
            }

          gabble_group_mixin_change_flags (G_OBJECT (chan), flags_add,
                                           flags_rem);

          if (status_code && strcmp (status_code, "201") == 0)
            {
              LmMessage *msg;
              GError *error;

              msg = lm_message_new_with_sub_type (priv->jid, LM_MESSAGE_TYPE_IQ,
                                                  LM_MESSAGE_SUB_TYPE_SET);

              node = lm_message_node_add_child (msg->node, "query", NULL);
              lm_message_node_set_attribute (node, "xmlns", MUC_XMLNS_OWNER);

              node = lm_message_node_add_child (node, "x", NULL);
              lm_message_node_set_attributes (node,
                                              "xmlns", "jabber:x:data",
                                              "type", "submit",
                                              NULL);

              if (!_gabble_connection_send_with_reply (priv->conn, msg,
                    room_created_submit_reply_cb, G_OBJECT (chan), NULL, &error))
                {
                  g_warning ("%s: failed to send submit message: %s", G_STRFUNC,
                             error->message);
                  g_error_free (error);

                  close_channel (chan, NULL, TRUE);
                }
            }

          /* Update room properties */
          room_properties_update (chan);
        }
    }
  else
    {
      LmMessageNode *reason_node;
      const gchar *reason = "";

      reason_node = lm_message_node_get_child (item_node, "reason");
      if (reason_node != NULL)
        {
          reason = lm_message_node_get_value (reason_node);
        }

      if (handle != mixin->self_handle)
        {
          gabble_group_mixin_change_members (G_OBJECT (chan), reason,
                                             empty, set, empty, empty);
        }
      else
        {
          close_channel (chan, reason, FALSE);
        }
    }

  g_intset_destroy (empty);
  g_intset_destroy (set);
}

static gboolean
queue_message (GabbleMucChannel *chan,
               TpChannelTextMessageType type,
               GabbleHandle sender,
               time_t timestamp,
               const gchar *text)
{
  GabbleMucChannelPrivate *priv;
  GabbleMucPendingMessage *msg;
  gsize len;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  msg = _gabble_muc_pending_new0 ();

  if (msg == NULL)
    {
      g_debug ("%s: no more pending messages available, giving up", G_STRFUNC);

      /* TODO: something clever here */

      return FALSE;
    }

  len = strlen (text);

  if (len > MAX_MESSAGE_SIZE)
    {
      g_debug ("%s: message exceeds maximum size, truncating", G_STRFUNC);

      /* TODO: something clever here */

      len = MAX_MESSAGE_SIZE;
    }

  msg->text = g_try_malloc (len + 1);

  if (msg->text == NULL)
    {
      g_debug ("%s: unable to allocate message, giving up", G_STRFUNC);

      _gabble_muc_pending_free (msg);

      /* TODO: something clever here */

      return FALSE;
    }

  g_strlcpy (msg->text, text, len + 1);

  msg->id = priv->recv_id++;
  msg->timestamp = timestamp;
  msg->sender = sender;
  msg->type = type;

  gabble_handle_ref (priv->conn->handles, TP_HANDLE_TYPE_CONTACT, msg->sender);
  g_queue_push_tail (priv->pending_messages, msg);

  g_signal_emit (chan, signals[RECEIVED], 0,
                 msg->id,
                 msg->timestamp,
                 msg->sender,
                 msg->type,
                 msg->text);

  g_debug ("%s: queued message %u", G_STRFUNC, msg->id);

  return TRUE;
}

static void
clear_message_queue (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv;
  GabbleMucPendingMessage *msg;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  while ((msg = g_queue_pop_head (priv->pending_messages)))
    {
      gabble_handle_unref (priv->conn->handles, TP_HANDLE_TYPE_CONTACT, msg->sender);
      _gabble_muc_pending_free (msg);
    }
}

static void set_properties_context_return (GabbleMucChannel *chan, GError *error);

/**
 * _gabble_muc_channel_receive
 */
gboolean
_gabble_muc_channel_receive (GabbleMucChannel *chan,
                             TpChannelTextMessageType type,
                             GabbleHandle sender,
                             time_t timestamp,
                             const gchar *text,
                             LmMessageNode *msg_node)
{
  GabbleMucChannelPrivate *priv;
  LmMessageNode *node;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  node = lm_message_node_get_child (msg_node, "subject");

  if (node)
    {
      GArray *changed_values, *changed_flags;
      GValue val = { 0, };

      priv->set_props_ctx.remaining &= ~(1 << ROOM_PROP_SUBJECT);

      if (lm_message_node_get_child (msg_node, "error"))
        {
          g_debug ("%s: error node present", G_STRFUNC);

          if (priv->set_props_ctx.call_ctx)
            {
              GError *error;

              /* FIXME: use gabble-error to parse the error node */
              error = g_error_new (TELEPATHY_ERRORS, PermissionDenied,
                  "failed to change subject: permission denied");

              set_properties_context_return (chan, error);
            }

          return TRUE;
        }

      changed_values = g_array_sized_new (FALSE, FALSE, sizeof (guint), 3);
      changed_flags = g_array_sized_new (FALSE, FALSE, sizeof (guint), 3);

      /* ROOM_PROP_SUBJECT */
      g_value_init (&val, G_TYPE_STRING);
      g_value_set_string (&val, lm_message_node_get_value (node));

      room_property_change_value (chan, ROOM_PROP_SUBJECT, &val, changed_values);
      room_property_change_flags (chan, ROOM_PROP_SUBJECT, TP_PROPERTY_FLAG_READ,
                                  0, changed_flags);

      g_value_unset (&val);

      /* ROOM_PROP_SUBJECT_CONTACT */
      g_value_init (&val, G_TYPE_UINT);
      g_value_set_uint (&val, sender);

      room_property_change_value (chan, ROOM_PROP_SUBJECT_CONTACT, &val,
          changed_values);
      room_property_change_flags (chan, ROOM_PROP_SUBJECT_CONTACT,
          TP_PROPERTY_FLAG_READ, 0, changed_flags);

      g_value_unset (&val);

      /* ROOM_PROP_SUBJECT_TIMESTAMP */
      g_value_init (&val, G_TYPE_UINT);
      g_value_set_uint (&val, timestamp);

      room_property_change_value (chan, ROOM_PROP_SUBJECT_TIMESTAMP, &val,
          changed_values);
      room_property_change_flags (chan, ROOM_PROP_SUBJECT_TIMESTAMP,
          TP_PROPERTY_FLAG_READ, 0, changed_flags);

      g_value_unset (&val);

      /* Emit signals */
      room_properties_emit_changed (chan, changed_values);
      room_properties_emit_flags (chan, changed_flags);

      g_array_free (changed_values, TRUE);
      g_array_free (changed_flags, TRUE);

      if (priv->set_props_ctx.call_ctx && priv->set_props_ctx.remaining == 0)
        {
          set_properties_context_return (chan, NULL);
        }

      return TRUE;
    }
  else if (sender == priv->handle)
    {
      HANDLER_DEBUG (msg_node, "ignoring message from channel");
      return TRUE;
    }

  return queue_message (chan, type, sender, timestamp, text);
}

void
_gabble_muc_channel_handle_invited (GabbleMucChannel *chan,
                                    GabbleHandle inviter,
                                    const gchar *message)
{
  GabbleMucChannelPrivate *priv;
  GIntSet *empty, *set_members, *set_pending;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  /* add ourself to local pending and the inviter to members */
  empty = g_intset_new ();
  set_members = g_intset_new ();
  set_pending = g_intset_new ();

  g_intset_add (set_members, inviter);
  g_intset_add (set_pending, priv->conn->self_handle);

  gabble_group_mixin_change_members (G_OBJECT (chan), message, set_members,
                                     empty, set_pending, empty);

  g_intset_destroy (empty);
  g_intset_destroy (set_members);
  g_intset_destroy (set_pending);

  /* queue the message */
  if (message && (message[0] != '\0'))
    {
      queue_message (chan, TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE, inviter,
                     time(NULL), message);
    }
}

static gint
compare_pending_message (gconstpointer haystack,
                         gconstpointer needle)
{
  const GabbleMucPendingMessage *msg = haystack;
  guint id = GPOINTER_TO_UINT (needle);

  return (msg->id != id);
}

/**
 * gabble_muc_channel_acknowledge_pending_message
 *
 * Implements DBus method AcknowledgePendingMessage
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_acknowledge_pending_message (GabbleMucChannel *obj, guint id, GError **error)
{
  GabbleMucChannelPrivate *priv;
  GList *node;
  GabbleMucPendingMessage *msg;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  node = g_queue_find_custom (priv->pending_messages,
                              GUINT_TO_POINTER (id),
                              compare_pending_message);

  if (node == NULL)
    {
      g_debug ("%s: invalid message id %u", G_STRFUNC, id);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "invalid message id %u", id);

      return FALSE;
    }

  msg = node->data;

  g_debug ("%s: acknowleding message id %u", G_STRFUNC, id);

  g_queue_remove (priv->pending_messages, msg);

  gabble_handle_unref (priv->conn->handles, TP_HANDLE_TYPE_CONTACT, msg->sender);
  _gabble_muc_pending_free (msg);

  return TRUE;
}


/**
 * gabble_muc_channel_add_members
 *
 * Implements DBus method AddMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_add_members (GabbleMucChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  return gabble_group_mixin_add_members (G_OBJECT (obj), contacts, message, error);
}


/**
 * gabble_muc_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_close (GabbleMucChannel *obj, GError **error)
{
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  g_debug ("%s called on %p", G_STRFUNC, obj);

  if (priv->closed)
    {
      g_debug ("%s: channel already closed", G_STRFUNC);

      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                            "Channel already closed");

      return FALSE;
    }

  close_channel (obj, NULL, TRUE);

  return TRUE;
}


/**
 * gabble_muc_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_get_channel_type (GabbleMucChannel *obj, gchar ** ret, GError **error)
{
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_TEXT);

  return TRUE;
}


/**
 * gabble_muc_channel_get_group_flags
 *
 * Implements DBus method GetGroupFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_get_group_flags (GabbleMucChannel *obj, guint* ret, GError **error)
{
  return gabble_group_mixin_get_group_flags (G_OBJECT (obj), ret, error);
}


/**
 * gabble_muc_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_get_handle (GabbleMucChannel *obj, guint* ret, guint* ret1, GError **error)
{
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  *ret = TP_HANDLE_TYPE_ROOM;
  *ret1 = priv->handle;

  return TRUE;
}


/**
 * gabble_muc_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_get_interfaces (GabbleMucChannel *obj, gchar *** ret, GError **error)
{
  const gchar *interfaces[] = {
      TP_IFACE_CHANNEL_INTERFACE_GROUP,
      TP_IFACE_CHANNEL_INTERFACE_PASSWORD,
      TP_IFACE_PROPERTIES,
      NULL
  };

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
}


/**
 * gabble_muc_channel_get_local_pending_members
 *
 * Implements DBus method GetLocalPendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_get_local_pending_members (GabbleMucChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_local_pending_members (G_OBJECT (obj), ret, error);
}


/**
 * gabble_muc_channel_get_members
 *
 * Implements DBus method GetMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_get_members (GabbleMucChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_members (G_OBJECT (obj), ret, error);
}




/**
 * gabble_muc_channel_get_password_flags
 *
 * Implements DBus method GetPasswordFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_get_password_flags (GabbleMucChannel *obj, guint* ret, GError **error)
{
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  *ret = priv->password_flags;

  return TRUE;
}


/**
 * gabble_muc_channel_get_remote_pending_members
 *
 * Implements DBus method GetRemotePendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_get_remote_pending_members (GabbleMucChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_remote_pending_members (G_OBJECT (obj), ret, error);
}


/**
 * gabble_muc_channel_get_self_handle
 *
 * Implements DBus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_get_self_handle (GabbleMucChannel *obj, guint* ret, GError **error)
{
  return gabble_group_mixin_get_self_handle (G_OBJECT (obj), ret, error);
}


/**
 * gabble_muc_channel_list_pending_messages
 *
 * Implements DBus method ListPendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_list_pending_messages (GabbleMucChannel *obj, GPtrArray ** ret, GError **error)
{
  GabbleMucChannelPrivate *priv;
  guint count;
  GPtrArray *messages;
  GList *cur;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  count = g_queue_get_length (priv->pending_messages);
  messages = g_ptr_array_sized_new (count);

  for (cur = g_queue_peek_head_link (priv->pending_messages);
       cur != NULL;
       cur = cur->next)
    {
      GabbleMucPendingMessage *msg = cur->data;
      GValue val = { 0, };

      g_value_init (&val, TP_TYPE_PENDING_MESSAGE_STRUCT);
      g_value_take_boxed (&val,
          dbus_g_type_specialized_construct (TP_TYPE_PENDING_MESSAGE_STRUCT));

      dbus_g_type_struct_set (&val,
                              0, msg->id,
                              1, msg->timestamp,
                              2, msg->sender,
                              3, msg->type,
                              4, msg->text,
                              G_MAXUINT);

      g_ptr_array_add (messages, g_value_get_boxed (&val));
    }

  *ret = messages;

  return TRUE;
}


/**
 * gabble_muc_channel_provide_password
 *
 * Implements DBus method ProvidePassword
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean gabble_muc_channel_provide_password (GabbleMucChannel *obj, const gchar * password, DBusGMethodInvocation *context)
{
  GError *error;
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  if ((priv->password_flags & TP_CHANNEL_PASSWORD_FLAG_PROVIDE) == 0 ||
      priv->password_ctx != NULL)
    {
      error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                           "password cannot be provided in the current state");
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return FALSE;
    }

  if (!send_join_request (obj, password, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return FALSE;
    }

  priv->password_ctx = context;

  return TRUE;
}


/**
 * gabble_muc_channel_remove_members
 *
 * Implements DBus method RemoveMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_remove_members (GabbleMucChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  return gabble_group_mixin_remove_members (G_OBJECT (obj), contacts, message, error);
}

/**
 * gabble_muc_channel_send
 *
 * Implements DBus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_send (GabbleMucChannel *obj, guint type, const gchar * text, GError **error)
{
  GabbleMucChannelPrivate *priv;
  LmMessage *msg;
  gboolean result;
  time_t timestamp;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  if (type > TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE)
    {
      g_debug ("%s: invalid message type %u", G_STRFUNC, type);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "invalid message type: %u", type);

      return FALSE;
    }

  msg = lm_message_new_with_sub_type (priv->jid, LM_MESSAGE_TYPE_MESSAGE,
                                      LM_MESSAGE_SUB_TYPE_GROUPCHAT);
  if (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION)
    {
      gchar *tmp;
      tmp = g_strconcat ("/me ", text, NULL);
      lm_message_node_add_child (msg->node, "body", tmp);
      g_free (tmp);
    }
  else
    {
      lm_message_node_add_child (msg->node, "body", text);
    }

  result = _gabble_connection_send (priv->conn, msg, error);
  lm_message_unref (msg);

  if (!result)
    return FALSE;

  timestamp = time (NULL);

  g_signal_emit (obj, signals[SENT], 0,
                 timestamp,
                 type,
                 text);

  return TRUE;
}


static gboolean
gabble_muc_channel_add_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error)
{
  GabbleMucChannelPrivate *priv;
  GabbleGroupMixin *mixin;
  const gchar *jid;
  LmMessage *msg;
  LmMessageNode *x_node, *invite_node;
  gboolean result;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  mixin = GABBLE_GROUP_MIXIN (obj);

  if (handle == priv->conn->self_handle)
    {
      GIntSet *set_empty, *set_members, *set_pending;
      GArray *arr_members;

      /* are we already a member or in remote pending? */
      if (handle_set_is_member (mixin->members, handle) ||
          handle_set_is_member (mixin->remote_pending, handle))
        {
          *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                                "already a member or in remote pending");

          return FALSE;
        }

      /* add ourself to remote pending and remove the inviter's
       * main jid from the member list */
      set_empty = g_intset_new ();
      set_members = g_intset_new ();
      set_pending = g_intset_new ();

      arr_members = handle_set_to_array (mixin->members);
      if (arr_members->len > 0)
        {
          g_intset_add (set_members, g_array_index (arr_members, guint32, 0));
        }
      g_array_free (arr_members, TRUE);

      g_intset_add (set_pending, handle);

      gabble_group_mixin_change_members (obj, "", set_empty, set_members,
                                         set_empty, set_pending);

      g_intset_destroy (set_empty);
      g_intset_destroy (set_members);
      g_intset_destroy (set_pending);

      /* seek to enter the room */
      result = send_join_request (GABBLE_MUC_CHANNEL (obj), NULL, error);

      g_object_set (obj, "state",
                    (result) ? MUC_STATE_INITIATED : MUC_STATE_ENDED,
                    NULL);

      /* deny adding */
      gabble_group_mixin_change_flags (obj, 0, TP_CHANNEL_GROUP_FLAG_CAN_ADD);

      /* clear message queue (which might contain an invite reason) */
      clear_message_queue (GABBLE_MUC_CHANNEL (obj));

      return result;
    }

  /* check that we're indeed a member when attempting to invite others */
  if (priv->state < MUC_STATE_JOINED)
    {
      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                            "channel membership is required for inviting others");

      return FALSE;
    }

  msg = lm_message_new (priv->jid, LM_MESSAGE_TYPE_MESSAGE);

  x_node = lm_message_node_add_child (msg->node, "x", NULL);
  lm_message_node_set_attribute (x_node, "xmlns", MUC_XMLNS_USER);

  invite_node = lm_message_node_add_child (x_node, "invite", NULL);

  jid = gabble_handle_inspect (GABBLE_GROUP_MIXIN (obj)->handle_repo,
                               TP_HANDLE_TYPE_CONTACT, handle);

  lm_message_node_set_attribute (invite_node, "to", jid);

  if (*message != '\0')
    {
      lm_message_node_add_child (invite_node, "reason", message);
    }

  HANDLER_DEBUG (msg->node, "sending MUC invitation");

  result = _gabble_connection_send (priv->conn, msg, error);
  lm_message_unref (msg);

  return result;
}

static LmHandlerResult
kick_request_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                       LmMessage *reply_msg, GObject *object,
                       gpointer user_data)
{
  const gchar *jid = user_data;

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      g_warning ("%s: Failed to kick user %s from room", G_STRFUNC, jid);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
gabble_muc_channel_remove_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error)
{
  GabbleMucChannelPrivate *priv;
  LmMessage *msg;
  LmMessageNode *query_node, *item_node;
  const gchar *jid;
  gchar *room, *server, *nick;
  gboolean result;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  msg = lm_message_new_with_sub_type (priv->jid, LM_MESSAGE_TYPE_IQ,
                                      LM_MESSAGE_SUB_TYPE_SET);

  query_node = lm_message_node_add_child (msg->node, "query", NULL);
  lm_message_node_set_attribute (query_node, "xmlns", MUC_XMLNS_ADMIN);

  item_node = lm_message_node_add_child (query_node, "item", NULL);

  jid = gabble_handle_inspect (GABBLE_GROUP_MIXIN (obj)->handle_repo,
                               TP_HANDLE_TYPE_CONTACT, handle);

  gabble_handle_decode_jid (jid, &room, &server, &nick);

  lm_message_node_set_attributes (item_node,
                                  "nick", nick,
                                  "role", "none",
                                  NULL);

  g_free (room);
  g_free (server);
  g_free (nick);

  if (*message != '\0')
    {
      lm_message_node_add_child (item_node, "reason", message);
    }

  HANDLER_DEBUG (msg->node, "sending MUC kick request");

  result = _gabble_connection_send_with_reply (priv->conn, msg,
                                               kick_request_reply_cb,
                                               obj, (gpointer) jid,
                                               error);

  lm_message_unref (msg);

  return result;
}


/**
 * gabble_muc_channel_list_properties
 *
 * Implements DBus method ListProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_list_properties (GabbleMucChannel *obj, GPtrArray ** ret, GError **error)
{
  GabbleMucChannelPrivate *priv;
  guint i;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  *ret = g_ptr_array_sized_new (NUM_ROOM_PROPS);

  for (i = 0; i < NUM_ROOM_PROPS; i++)
    {
      RoomProperty *prop = &priv->room_props[i];
      const gchar *dbus_sig;
      GValue val = { 0, };

      switch (room_property_signatures[i].type) {
        case G_TYPE_BOOLEAN:
          dbus_sig = "b";
          break;
        case G_TYPE_UINT:
          dbus_sig = "u";
          break;
        case G_TYPE_STRING:
          dbus_sig = "s";
          break;
        default:
          g_assert_not_reached ();
      };

      g_value_init (&val, TP_TYPE_PROPERTY_INFO_STRUCT);
      g_value_set_static_boxed (&val,
          dbus_g_type_specialized_construct (TP_TYPE_PROPERTY_INFO_STRUCT));

      dbus_g_type_struct_set (&val,
          0, i,
          1, room_property_signatures[i].name,
          2, dbus_sig,
          3, prop->flags,
          G_MAXUINT);

      g_ptr_array_add (*ret, g_value_get_boxed (&val));
    }

  return TRUE;
}


/**
 * gabble_muc_channel_get_properties
 *
 * Implements DBus method GetProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_get_properties (GabbleMucChannel *obj, const GArray * properties, GPtrArray ** ret, GError **error)
{
  GabbleMucChannelPrivate *priv;
  guint i;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  /* Check input property identifiers */
  for (i = 0; i < properties->len; i++)
    {
      guint prop_id = g_array_index (properties, guint, i);

      /* Valid? */
      if (prop_id >= NUM_ROOM_PROPS)
        {
          *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                                "invalid property identifier %d", prop_id);

          return FALSE;
        }

      /* Permitted? */
      if (!(priv->room_props[prop_id].flags & TP_PROPERTY_FLAG_READ))
        {
          *error = g_error_new (TELEPATHY_ERRORS, PermissionDenied,
                                "permission denied for property identifier %d", prop_id);

          return FALSE;
        }
    }

  /* If we got this far, return the actual values */
  *ret = g_ptr_array_sized_new (properties->len);

  for (i = 0; i < properties->len; i++)
    {
      guint prop_id = g_array_index (properties, guint, i);
      GValue val_struct = { 0, };

      /* id/value struct */
      g_value_init (&val_struct, TP_TYPE_PROPERTY_VALUE_STRUCT);
      g_value_set_static_boxed (&val_struct,
          dbus_g_type_specialized_construct (TP_TYPE_PROPERTY_VALUE_STRUCT));

      dbus_g_type_struct_set (&val_struct,
          0, prop_id,
          1, &priv->room_props[prop_id].value,
          G_MAXUINT);

      g_ptr_array_add (*ret, g_value_get_boxed (&val_struct));
    }

  return TRUE;
}

static void set_properties_context_return (GabbleMucChannel *chan, GError *error)
{
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);
  SetPropertiesContext *ctx = &priv->set_props_ctx;
  GArray *changed_props_val, *changed_props_flags;
  int i, n;

  g_debug ("%s: %s", G_STRFUNC, (error) ? "failure" : "success");

  changed_props_val = g_array_sized_new (FALSE, FALSE, sizeof (guint),
                                         NUM_ROOM_PROPS);
  changed_props_flags = g_array_sized_new (FALSE, FALSE, sizeof (guint),
                                           NUM_ROOM_PROPS);

  for (i = n = 0; i < NUM_ROOM_PROPS; i++)
    {
      if (ctx->values[i])
        {
          if (!error && i != ROOM_PROP_SUBJECT)
            {
              room_property_change_value (chan, i, ctx->values[i],
                                          changed_props_val);

              room_property_change_flags (chan, i, TP_PROPERTY_FLAG_READ,
                                          0, changed_props_flags);

              n++;
            }

          g_value_unset (ctx->values[i]);
        }
    }

  if (!error)
    {
      if (n)
        {
          room_properties_emit_changed (chan, changed_props_val);
          room_properties_emit_flags (chan, changed_props_flags);
        }

      dbus_g_method_return (ctx->call_ctx);
    }
  else
    {
      dbus_g_method_return_error (ctx->call_ctx, error);
      g_error_free (error);

      /* Get the properties into a consistent state. */
      room_properties_update (chan);
    }

  memset (ctx, 0, sizeof (SetPropertiesContext));

  g_array_free (changed_props_val, TRUE);
  g_array_free (changed_props_flags, TRUE);
}

static LmHandlerResult request_config_form_reply_cb (GabbleConnection *conn, LmMessage *sent_msg, LmMessage *reply_msg, GObject *object, gpointer user_data);

/**
 * gabble_muc_channel_set_properties
 *
 * Implements DBus method SetProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean gabble_muc_channel_set_properties (GabbleMucChannel *obj, const GPtrArray * properties, DBusGMethodInvocation *context)
{
  GabbleMucChannelPrivate *priv;
  gboolean result;
  GError *error;
  LmMessage *msg;
  LmMessageNode *node;
  guint i;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  /* Is another SetProperties request already in progress? */
  if (priv->set_props_ctx.call_ctx)
    {
      error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                           "A SetProperties request is already in progress");
      goto ERROR;
    }

  priv->set_props_ctx.call_ctx = context;
  result = TRUE;
  error = NULL;

  /* Check input property identifiers */
  for (i = 0; i < properties->len; i++)
    {
      GValue val_struct = { 0, };
      guint prop_id;
      GValue *prop_val;

      g_value_init (&val_struct, TP_TYPE_PROPERTY_VALUE_STRUCT);
      g_value_set_static_boxed (&val_struct, g_ptr_array_index (properties, i));

      dbus_g_type_struct_get (&val_struct,
          0, &prop_id,
          1, &prop_val,
          G_MAXUINT);

      /* Valid? */
      if (prop_id >= NUM_ROOM_PROPS)
        {
          g_value_unset (prop_val);

          error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                               "invalid property identifier %d", prop_id);
          goto ERROR;
        }

      /* Store the value in the context */
      priv->set_props_ctx.remaining |= 1 << prop_id;
      priv->set_props_ctx.values[prop_id] = prop_val;

      /* Permitted? */
      if (!(priv->room_props[prop_id].flags & TP_PROPERTY_FLAG_WRITE))
        {
          error = g_error_new (TELEPATHY_ERRORS, PermissionDenied,
                               "permission denied for property identifier %d", prop_id);
          goto ERROR;
        }

      /* Compatible type? */
      if (!g_value_type_compatible (G_VALUE_TYPE (prop_val),
                                    G_VALUE_TYPE (&priv->room_props[prop_id].value)))
        {
          error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                               "incompatible value type for property identifier %d",
                               prop_id);
          goto ERROR;
        }
    }

  /* Changing subject? */
  if (priv->set_props_ctx.values[ROOM_PROP_SUBJECT])
    {
      const gchar *str;

      str = g_value_get_string (priv->set_props_ctx.values[ROOM_PROP_SUBJECT]);

      msg = lm_message_new_with_sub_type (priv->jid,
          LM_MESSAGE_TYPE_MESSAGE, LM_MESSAGE_SUB_TYPE_GROUPCHAT);
      lm_message_node_add_child (msg->node, "subject", str);

      _gabble_connection_send (priv->conn, msg, &error);

      lm_message_unref (msg);

      if (error)
        goto ERROR;
    }

  /* Changing any other properties? */
  if ((priv->set_props_ctx.remaining & ~(1 << ROOM_PROP_SUBJECT)) != 0)
    {
      msg = lm_message_new_with_sub_type (priv->jid,
          LM_MESSAGE_TYPE_IQ, LM_MESSAGE_SUB_TYPE_GET);
      node = lm_message_node_add_child (msg->node, "query", NULL);
      lm_message_node_set_attribute (node, "xmlns", MUC_XMLNS_OWNER);

       _gabble_connection_send_with_reply (priv->conn, msg,
          request_config_form_reply_cb, G_OBJECT (obj), &priv->set_props_ctx,
          &error);

      lm_message_unref (msg);

      if (error)
        goto ERROR;
    }

  goto OUT;

ERROR:
  set_properties_context_return (obj, error);
  result = FALSE;

OUT:
  return result;
}

static LmHandlerResult request_config_form_submit_reply_cb (GabbleConnection *conn, LmMessage *sent_msg, LmMessage *reply_msg, GObject *object, gpointer user_data);

static LmHandlerResult
request_config_form_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                              LmMessage *reply_msg, GObject *object,
                              gpointer user_data)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);
  SetPropertiesContext *ctx = user_data;
  GError *error = NULL;
  LmMessage *msg = NULL;
  LmMessageNode *submit_node, *query_node, *form_node, *node;

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      error = g_error_new (TELEPATHY_ERRORS, PermissionDenied,
                           "request for configuration form denied");

      goto OUT;
    }

  /* initialize */
  msg = lm_message_new_with_sub_type (priv->jid, LM_MESSAGE_TYPE_IQ,
                                      LM_MESSAGE_SUB_TYPE_SET);

  node = lm_message_node_add_child (msg->node, "query", NULL);
  lm_message_node_set_attribute (node, "xmlns", MUC_XMLNS_OWNER);

  submit_node = lm_message_node_add_child (node, "x", NULL);
  lm_message_node_set_attributes (submit_node,
                                  "xmlns", "jabber:x:data",
                                  "type", "submit",
                                  NULL);

  /* find the query node */
  query_node = lm_message_node_get_child (reply_msg->node, "query");
  if (query_node == NULL)
    goto PARSE_ERROR;

  /* then the form node */
  form_node = NULL;
  for (node = query_node->children; node; node = node->next)
    {
      if (strcmp (node->name, "x") == 0)
        {
          if (strcmp (lm_message_node_get_attribute (node, "xmlns"),
                      "jabber:x:data") != 0)
            {
              continue;
            }

          if (strcmp (lm_message_node_get_attribute (node, "type"),
                      "form") != 0)
            {
              continue;
            }

          form_node = node;
          break;
        }
    }

  if (form_node == NULL)
    goto PARSE_ERROR;

  for (node = form_node->children; node; node = node->next)
    {
      const gchar *var, *prev_value;
      LmMessageNode *field_node, *value_node;
      guint id;
      GType type;
      gboolean invert;
      gchar buf[16];
      const gchar *val_str;
      gboolean val_bool;

      if (strcmp (node->name, "field") != 0)
        {
          g_debug ("%s: skipping node '%s'", G_STRFUNC, node->name);
          continue;
        }

      var = lm_message_node_get_attribute (node, "var");
      if (var == NULL) {
        g_debug ("%s: skipping node '%s' because of lacking var attribute",
                 G_STRFUNC, node->name);
        continue;
      }

      value_node = lm_message_node_get_child (node, "value");
      if (value_node == NULL)
        {
          g_debug ("%s: skipping var '%s' because of lacking value attribute",
                   G_STRFUNC, var);
          continue;
        }

      prev_value = lm_message_node_get_value (value_node);

      /* add the corresponding field node to the reply message */
      field_node = lm_message_node_add_child (submit_node, "field", NULL);

      lm_message_node_set_attribute (field_node, "var", var);

      val_str = lm_message_node_get_attribute (node, "type");
      if (val_str)
        {
          lm_message_node_set_attribute (field_node, "type", val_str);
        }

      value_node = lm_message_node_add_child (field_node, "value", prev_value);

      id = INVALID_ROOM_PROP;
      type = G_TYPE_BOOLEAN;
      invert = FALSE;
      val_str = NULL;

      if (strcmp (var, "anonymous") == 0)
        {
          id = ROOM_PROP_ANONYMOUS;
        }
      else if (strcmp (var, "muc#owner_whois") == 0)
        {
          id = ROOM_PROP_ANONYMOUS;

          val_bool = g_value_get_boolean (ctx->values[id]);
          val_str = (val_bool) ? "admins" : "anyone";
        }
      else if (strcmp (var, "members_only") == 0 ||
               strcmp (var, "muc#owner_inviteonly") == 0)
        {
          id = ROOM_PROP_INVITE_ONLY;
        }
      else if (strcmp (var, "moderated") == 0 ||
               strcmp (var, "muc#owner_moderatedroom") == 0)
        {
          id = ROOM_PROP_MODERATED;
        }
      else if (strcmp (var, "title") == 0 ||
               strcmp (var, "muc#owner_roomname") == 0)
        {
          id = ROOM_PROP_NAME;
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
               strcmp (var, "muc#owner_passwordprotectedroom") == 0)
        {
          id = ROOM_PROP_PASSWORD_REQUIRED;
        }
      else if (strcmp (var, "persistent") == 0 ||
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
          g_warning ("%s: ignoring field '%s'", G_STRFUNC, var);
          continue;
        }

      g_debug ("%s: looking up %s", G_STRFUNC, room_property_signatures[id].name);

      if (!ctx->values[id])
        continue;

      if (!val_str)
        {
          switch (type) {
            case G_TYPE_BOOLEAN:
              val_bool = g_value_get_boolean (ctx->values[id]);
              sprintf (buf, "%d", (invert) ? !val_bool : val_bool);
              val_str = buf;
              break;
            case G_TYPE_STRING:
              val_str = g_value_get_string (ctx->values[id]);
              break;
            default:
              g_assert_not_reached ();
          }
        }

      lm_message_node_set_value (value_node, val_str);

      ctx->remaining &= ~(1 << id);
    }

  if ((ctx->remaining & ~(1 << ROOM_PROP_SUBJECT)) != 0)
    {
      gint i;

      printf (ANSI_BOLD_ON ANSI_FG_WHITE ANSI_BG_RED
              "\n%s: the following properties were not substituted:\n",
              G_STRFUNC);

      for (i = 0; i < NUM_ROOM_PROPS; i++)
        {
          if (ctx->remaining & (1 << i))
            {
              printf ("  %s\n", room_property_signatures[i].name);
            }
        }

      printf ("\nthis is a MUC server compatibility bug in gabble, please "
              "report it with a full debug log attached (running gabble "
              "with LM_DEBUG=net)" ANSI_RESET "\n\n");
      fflush (stdout);

      error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                           "not all properties were substituted");
      goto OUT;
    }

  _gabble_connection_send_with_reply (priv->conn, msg,
      request_config_form_submit_reply_cb, G_OBJECT (object),
      ctx, &error);

  goto OUT;

PARSE_ERROR:
  error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                       "error parsing reply from server");

OUT:
  if (error)
    set_properties_context_return (chan, error);

  if (msg)
    lm_message_unref (msg);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static LmHandlerResult
request_config_form_submit_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                                     LmMessage *reply_msg, GObject *object,
                                     gpointer user_data)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (object);
  SetPropertiesContext *ctx = user_data;
  GError *error = NULL;

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      error = g_error_new (TELEPATHY_ERRORS, PermissionDenied,
                           "submitted configuration form was rejected");
    }

  if (ctx->remaining == 0 || error)
    set_properties_context_return (chan, error);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

#define RPTS_APPEND_FLAG_IF_SET(flag) \
  if (flags & flag) \
    { \
      if (i++ > 0) \
        g_string_append (str, "|"); \
      g_string_append (str, #flag + 17); \
    }

static gchar *
room_property_flags_to_string (TpPropertyFlags flags)
{
  gint i = 0;
  GString *str;

  str = g_string_new ("[" ANSI_BOLD_OFF);

  RPTS_APPEND_FLAG_IF_SET (TP_PROPERTY_FLAG_READ);
  RPTS_APPEND_FLAG_IF_SET (TP_PROPERTY_FLAG_WRITE);

  g_string_append (str, ANSI_BOLD_ON "]");

  return g_string_free (str, FALSE);
}

static void
room_property_change_value (GabbleMucChannel *chan,
                            guint prop_id,
                            const GValue *new_value,
                            GArray *props)
{
  GabbleMucChannelPrivate *priv;
  GValue *cur_val;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  cur_val = &priv->room_props[prop_id].value;

  g_value_copy (new_value, cur_val);

  if (props)
    {
      g_array_append_val (props, prop_id);
    }
  else
    {
      GArray *changed_props = g_array_sized_new (FALSE, FALSE,
                                                 sizeof (guint), 1);
      g_array_append_val (changed_props, prop_id);

      room_properties_emit_changed (chan, changed_props);

      g_array_free (changed_props, TRUE);
    }
}

static void
room_property_change_flags (GabbleMucChannel *chan,
                            guint prop_id,
                            TpPropertyFlags add,
                            TpPropertyFlags remove,
                            GArray *props)
{
  GabbleMucChannelPrivate *priv;
  RoomProperty *prop;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  prop = &priv->room_props[prop_id];

  prop->flags |= add;
  prop->flags &= ~remove;

  if (add != 0 || remove != 0)
    {
      if (props)
        {
          g_array_append_val (props, prop_id);
        }
      else
        {
          GArray *changed_props = g_array_sized_new (FALSE, FALSE,
                                                     sizeof (guint), 1);
          g_array_append_val (changed_props, prop_id);

          room_properties_emit_flags (chan, changed_props);

          g_array_free (changed_props, TRUE);
        }
    }
}

static void
room_properties_emit_changed (GabbleMucChannel *chan, GArray *props)
{
  GabbleMucChannelPrivate *priv;
  GPtrArray *prop_arr;
  GValue prop_list = { 0, };
  guint i;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  prop_arr = g_ptr_array_sized_new (props->len);

  printf (ANSI_BOLD_ON ANSI_FG_CYAN
          "%s: emitting room properties changed for propert%s:\n",
          G_STRFUNC, (props->len > 1) ? "ies" : "y");

  for (i = 0; i < props->len; i++)
    {
      GValue prop_val = { 0, };
      guint prop_id = g_array_index (props, guint, i);

      g_value_init (&prop_val, TP_TYPE_PROPERTY_VALUE_STRUCT);
      g_value_set_static_boxed (&prop_val,
          dbus_g_type_specialized_construct (TP_TYPE_PROPERTY_VALUE_STRUCT));

      dbus_g_type_struct_set (&prop_val,
          0, prop_id,
          1, &priv->room_props[prop_id].value,
          G_MAXUINT);

      g_ptr_array_add (prop_arr, g_value_get_boxed (&prop_val));

      printf ("  %s\n", room_property_signatures[prop_id].name);
    }

  printf (ANSI_RESET);
  fflush (stdout);

  g_signal_emit (chan, signals[PROPERTIES_CHANGED], 0, prop_arr);

  g_value_init (&prop_list, TP_TYPE_PROPERTY_VALUE_LIST);
  g_value_set_static_boxed (&prop_list, prop_arr);
  g_value_unset (&prop_list);
}

static void
room_properties_emit_flags (GabbleMucChannel *chan, GArray *props)
{
  GabbleMucChannelPrivate *priv;
  GPtrArray *prop_arr;
  GValue prop_list = { 0, };
  guint i;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  prop_arr = g_ptr_array_sized_new (props->len);

  printf (ANSI_BOLD_ON ANSI_FG_WHITE
          "%s: emitting room properties flags changed for propert%s:\n",
          G_STRFUNC, (props->len > 1) ? "ies" : "y");

  for (i = 0; i < props->len; i++)
    {
      GValue prop_val = { 0, };
      guint prop_id = g_array_index (props, guint, i);
      guint prop_flags;
      gchar *str_flags;

      prop_flags = priv->room_props[prop_id].flags;

      g_value_init (&prop_val, TP_TYPE_PROPERTY_FLAGS_STRUCT);
      g_value_set_static_boxed (&prop_val,
          dbus_g_type_specialized_construct (TP_TYPE_PROPERTY_FLAGS_STRUCT));

      dbus_g_type_struct_set (&prop_val,
          0, prop_id,
          1, prop_flags,
          G_MAXUINT);

      g_ptr_array_add (prop_arr, g_value_get_boxed (&prop_val));

      str_flags = room_property_flags_to_string (prop_flags);

      printf ("  %s's flags now: %s\n",
          room_property_signatures[prop_id].name,
          str_flags);

      g_free (str_flags);
    }

  printf (ANSI_RESET);
  fflush (stdout);

  g_signal_emit (chan, signals[PROPERTY_FLAGS_CHANGED], 0, prop_arr);

  g_value_init (&prop_list, TP_TYPE_PROPERTY_FLAGS_LIST);
  g_value_set_static_boxed (&prop_list, prop_arr);
  g_value_unset (&prop_list);
}

