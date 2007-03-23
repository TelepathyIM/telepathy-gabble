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

#define DEBUG_FLAG GABBLE_DEBUG_MUC

#include <telepathy-glib/debug-ansi.h>
#include "debug.h"
#include "disco.h"
#include "gabble-connection.h"
#include "gabble-error.h"
#include "namespaces.h"
#include "util.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>

#include "gabble-muc-channel.h"

#define DEFAULT_JOIN_TIMEOUT (180 * 1000)
#define MAX_NICK_RETRIES 3

#define PROPS_POLL_INTERVAL_LOW  (60 * 1000 * 5)
#define PROPS_POLL_INTERVAL_HIGH (60 * 1000)

static void channel_iface_init (gpointer, gpointer);
static void password_iface_init (gpointer, gpointer);
static void text_iface_init (gpointer, gpointer);
static void chat_state_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleMucChannel, gabble_muc_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL,
      channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_PROPERTIES_INTERFACE,
      tp_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
      tp_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_PASSWORD,
      password_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT,
      text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_CHAT_STATE,
      chat_state_iface_init)
    )

/* signal enum */
enum
{
    READY,
    JOIN_ERROR,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONNECTION,
  PROP_STATE,
  PROP_INVITE_SELF,
  LAST_PROPERTY
};

typedef enum {
    MUC_STATE_CREATED = 0,
    MUC_STATE_INITIATED,
    MUC_STATE_AUTH,
    MUC_STATE_JOINED,
    MUC_STATE_ENDED,
} GabbleMucState;

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
      { "anonymous",         G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "invite-only",       G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "moderated",         G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "name",              G_TYPE_STRING },   /* impl: READ, WRITE */
      { "description",       G_TYPE_STRING },   /* impl: READ, WRITE */
      { "password",          G_TYPE_STRING },   /* impl: WRITE */
      { "password-required", G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "persistent",        G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "private",           G_TYPE_BOOLEAN },  /* impl: READ, WRITE */
      { "subject",           G_TYPE_STRING },   /* impl: READ, WRITE */
      { "subject-contact",   G_TYPE_UINT },     /* impl: READ */
      { "subject-timestamp", G_TYPE_UINT },     /* impl: READ */
};

/* private structures */

typedef struct _GabbleMucChannelPrivate GabbleMucChannelPrivate;

struct _GabbleMucChannelPrivate
{
  GabbleConnection *conn;
  gchar *object_path;

  GabbleMucState state;

  guint join_timer_id;
  guint poll_timer_id;

  TpChannelPasswordFlags password_flags;
  DBusGMethodInvocation *password_ctx;
  gchar *password;

  TpHandle handle;
  const gchar *jid;

  guint nick_retry_count;
  GString *self_jid;
  GabbleMucRole self_role;
  GabbleMucAffiliation self_affil;

  guint recv_id;

  TpPropertiesContext *properties_ctx;

  gboolean ready_emitted;

  gboolean closed;
  gboolean dispose_has_run;

  gboolean invite_self;
};

#define GABBLE_MUC_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MUC_CHANNEL, GabbleMucChannelPrivate))

static void
gabble_muc_channel_init (GabbleMucChannel *obj)
{
  /* do nothing? */
}

static void contact_handle_to_room_identity (GabbleMucChannel *, TpHandle, TpHandle *, GString **);

static GObject *
gabble_muc_channel_constructor (GType type, guint n_props,
                                GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMucChannelPrivate *priv;
  TpBaseConnection *conn;
  DBusGConnection *bus;
  TpHandleRepoIface *room_handles, *contact_handles;
  TpHandle self_handle;
  gboolean valid;

  obj = G_OBJECT_CLASS (gabble_muc_channel_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);
  conn = (TpBaseConnection *)priv->conn;

  room_handles = tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_ROOM);
  contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  /* ref our room handle */
  valid = tp_handle_ref (room_handles, priv->handle);
  g_assert (valid);

  /* get the room's jid */
  priv->jid = tp_handle_inspect (room_handles, priv->handle);

  /* get our own identity in the room */
  contact_handle_to_room_identity (GABBLE_MUC_CHANNEL (obj),
      conn->self_handle, &self_handle, &priv->self_jid);

  valid = tp_handle_ref (contact_handles, self_handle);
  g_assert (valid);

  /* initialize our own role and affiliation */
  priv->self_role = ROLE_NONE;
  priv->self_affil = AFFILIATION_NONE;

  /* register object on the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  /* initialize group mixin */
  tp_group_mixin_init ((TpSvcChannelInterfaceGroup *)obj,
      G_STRUCT_OFFSET (GabbleMucChannel, group),
      contact_handles, self_handle);

  /* set initial group flags */
  tp_group_mixin_change_flags ((TpSvcChannelInterfaceGroup *)obj,
      TP_CHANNEL_GROUP_FLAG_CHANNEL_SPECIFIC_HANDLES |
      TP_CHANNEL_GROUP_FLAG_CAN_ADD,
      0);

  /* initialize properties mixin */
  tp_properties_mixin_init (obj, G_STRUCT_OFFSET (
        GabbleMucChannel, properties));

  /* initialize text mixin */
  gabble_text_mixin_init (obj, G_STRUCT_OFFSET (GabbleMucChannel, text),
      contact_handles, FALSE);

  tp_text_mixin_set_message_types (obj,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
      G_MAXUINT);

  /* add ourselves to group mixin if needed */
  if (priv->invite_self)
    {
      GError *error = NULL;
      GArray *members = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);
      g_array_append_val (members, self_handle);
      tp_group_mixin_add_members ((TpSvcChannelInterfaceGroup *)obj, members,
          "", &error);
      g_assert (error == NULL);
      g_array_free (members, TRUE);
    }
  return obj;
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
  GabbleMucChannelPrivate *priv;
  GArray *changed_props_val, *changed_props_flags;
  LmMessageNode *lm_node;
  const gchar *str;
  GValue val = { 0, };

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  if (error)
    {
      DEBUG ("got error %s", error->message);
      return;
    }

  NODE_DEBUG (query_result, "disco query result");

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
      const gchar *type, *category, *name;

      type = lm_message_node_get_attribute (lm_node, "type");
      category = lm_message_node_get_attribute (lm_node, "category");
      name = lm_message_node_get_attribute (lm_node, "name");

      if (NULL != type && 0 == strcmp (type, "text") &&
          NULL != category && 0 == strcmp (category, "conference") &&
          NULL != name)
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

  for (lm_node = query_result->children; lm_node; lm_node = lm_node->next)
    {
      guint prop_id = INVALID_ROOM_PROP;

      if (strcmp (lm_node->name, "feature") == 0)
        {
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
          else if (strcmp (str, NS_MUC) == 0)
            {
            }

          /* Unhandled */
          else
            {
              g_warning ("%s: unhandled feature '%s'", G_STRFUNC, str);
            }
        }
      else if (strcmp (lm_node->name, "x") == 0)
        {
          if (lm_message_node_has_namespace (lm_node, NS_X_DATA, NULL))
            {
              LmMessageNode *field, *value_node;

              for (field = lm_node->children; field; field = field->next)
                {
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
  g_array_free (changed_props_val, TRUE);
  g_array_free (changed_props_flags, TRUE);
}

static void
room_properties_update (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv;
  GError *error = NULL;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  if (gabble_disco_request (priv->conn->disco, GABBLE_DISCO_TYPE_INFO,
        priv->jid, NULL, properties_disco_cb, chan, G_OBJECT (chan),
        &error) == NULL)
    {
      g_warning ("%s: disco query failed: '%s'", G_STRFUNC, error->message);
      g_error_free (error);
    }
}

static void
contact_handle_to_room_identity (GabbleMucChannel *chan, TpHandle main_handle,
                                 TpHandle *room_handle, GString **room_jid)
{
  GabbleMucChannelPrivate *priv;
  TpBaseConnection *conn;
  TpHandleRepoIface *contact_repo;
  const gchar *main_jid;
  gchar *username, *server;
  gchar *jid;

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);
  conn = (TpBaseConnection *)priv->conn;
  contact_repo = tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT);

  main_jid = tp_handle_inspect (contact_repo, main_handle);

  gabble_decode_jid (main_jid, &username, &server, NULL);

  jid = g_strdup_printf ("%s/%s", priv->jid, username);

  g_free (username);
  g_free (server);

  if (room_handle)
    {
      *room_handle = tp_handle_ensure (contact_repo, jid,
          GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);
    }

  if (room_jid)
    {
      *room_jid = g_string_new (jid);
    }

  g_free (jid);
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
  msg = lm_message_new (priv->self_jid->str, LM_MESSAGE_TYPE_PRESENCE);

  x_node = lm_message_node_add_child (msg->node, "x", NULL);
  lm_message_node_set_attribute (x_node, "xmlns", NS_MUC);

  g_free (priv->password);

  if (password != NULL)
    {
      priv->password = g_strdup (password);
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
      DEBUG ("join request sent");
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
  GError *error = NULL;
  gboolean ret;

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (channel);

  /* build the message */
  msg = lm_message_new_with_sub_type (priv->self_jid->str,
                                      LM_MESSAGE_TYPE_PRESENCE,
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
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_ROOM);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
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
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      priv->handle = g_value_get_uint (value);
      break;
    case PROP_HANDLE_TYPE:
      /* this property is writable in the interface, but not actually
       * meaningfully changable on this channel, so we do nothing */
      break;
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    case PROP_STATE:
      prev_state = priv->state;
      priv->state = g_value_get_uint (value);

      if (priv->state != prev_state)
        channel_state_changed (chan, prev_state, priv->state);

      break;
    case PROP_INVITE_SELF:
      priv->invite_self = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_muc_channel_dispose (GObject *object);
static void gabble_muc_channel_finalize (GObject *object);
static gboolean gabble_muc_channel_add_member (TpSvcChannelInterfaceGroup *obj, TpHandle handle, const gchar *message, GError **error);
static gboolean gabble_muc_channel_remove_member (TpSvcChannelInterfaceGroup *obj, TpHandle handle, const gchar *message, GError **error);
static gboolean gabble_muc_channel_do_set_properties (GObject *obj, TpPropertiesContext *ctx, GError **error);

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

  g_object_class_override_property (object_class, PROP_OBJECT_PATH, "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE, "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE, "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "MUC channel object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_uint ("state", "Channel state",
                                  "The current state that the channel is in.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  param_spec = g_param_spec_boolean ("invite-self", "Invite self",
                                     "Whether the user should be added to members list.",
                                     FALSE,
                                     G_PARAM_CONSTRUCT_ONLY |
                                     G_PARAM_WRITABLE |
                                     G_PARAM_STATIC_NAME |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_INVITE_SELF, param_spec);

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

  tp_group_mixin_class_init ((TpSvcChannelInterfaceGroupClass *)object_class,
                                 G_STRUCT_OFFSET (GabbleMucChannelClass, group_class),
                                 gabble_muc_channel_add_member,
                                 gabble_muc_channel_remove_member);

  tp_properties_mixin_class_init (object_class,
                                      G_STRUCT_OFFSET (GabbleMucChannelClass, properties_class),
                                      room_property_signatures, NUM_ROOM_PROPS,
                                      gabble_muc_channel_do_set_properties);

  tp_text_mixin_class_init (object_class, G_STRUCT_OFFSET (GabbleMucChannelClass, text_class));
}

static void clear_join_timer (GabbleMucChannel *chan);
static void clear_poll_timer (GabbleMucChannel *chan);

void
gabble_muc_channel_dispose (GObject *object)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  DEBUG ("called");

  priv->dispose_has_run = TRUE;

  clear_join_timer (self);
  clear_poll_timer (self);

  if (G_OBJECT_CLASS (gabble_muc_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_muc_channel_parent_class)->dispose (object);
}

void
gabble_muc_channel_finalize (GObject *object)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *room_handles = tp_base_connection_get_handles (
      (TpBaseConnection *)priv->conn, TP_HANDLE_TYPE_ROOM);

  DEBUG ("called");

  /* free any data held directly by the object here */
  tp_handle_unref (room_handles, priv->handle);

  g_free (priv->object_path);

  if (priv->self_jid)
    {
      g_string_free (priv->self_jid, TRUE);
    }

  g_free (priv->password);

  tp_properties_mixin_finalize (object);

  tp_group_mixin_finalize ((TpSvcChannelInterfaceGroup *)object);

  tp_text_mixin_finalize (object);

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

static void clear_poll_timer (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  if (priv->poll_timer_id != 0)
    {
      g_source_remove (priv->poll_timer_id);
      priv->poll_timer_id = 0;
    }
}

static void
change_password_flags (GabbleMucChannel *chan,
                       TpChannelPasswordFlags add,
                       TpChannelPasswordFlags remove)
{
  GabbleMucChannelPrivate *priv;
  TpChannelPasswordFlags added, removed;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  added = add & ~priv->password_flags;
  priv->password_flags |= added;

  removed = remove & priv->password_flags;
  priv->password_flags &= ~removed;

  if (add != 0 || remove != 0)
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

static void close_channel (GabbleMucChannel *chan, const gchar *reason, gboolean inform_muc, TpHandle actor, guint reason_code);

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
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  DEBUG ("state changed from %s to %s", muc_states[prev_state], muc_states[new_state]);

  if (new_state == MUC_STATE_INITIATED)
    {
      priv->join_timer_id =
        g_timeout_add (DEFAULT_JOIN_TIMEOUT, timeout_join, chan);
    }
  else if (new_state == MUC_STATE_JOINED)
    {
      gboolean low_bandwidth;
      gint interval;

      provide_password_return_if_pending (chan, TRUE);

      clear_join_timer (chan);

      g_object_get (priv->conn, "low-bandwidth", &low_bandwidth, NULL);

      if (low_bandwidth)
        interval = PROPS_POLL_INTERVAL_LOW;
      else
        interval = PROPS_POLL_INTERVAL_HIGH;

      priv->poll_timer_id = g_timeout_add (interval, timeout_poll, chan);

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
      if (!priv->ready_emitted)
        {
          g_signal_emit (chan, signals[READY], 0);

          priv->ready_emitted = TRUE;
        }
    }
}


static void
close_channel (GabbleMucChannel *chan, const gchar *reason,
               gboolean inform_muc, TpHandle actor, guint reason_code)
{
  GabbleMucChannelPrivate *priv;
  TpIntSet *set;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  if (priv->closed)
    return;

  priv->closed = TRUE;

  /* Remove us from member list */
  set = tp_intset_new ();
  tp_intset_add (set, TP_GROUP_MIXIN (chan)->self_handle);

  tp_group_mixin_change_members ((TpSvcChannelInterfaceGroup *)chan,
                                     (reason != NULL) ? reason : "",
                                     NULL, set, NULL, NULL, actor,
                                     reason_code);

  tp_intset_destroy (set);

  /* Inform the MUC if requested */
  if (inform_muc && priv->state >= MUC_STATE_INITIATED)
    {
      send_leave_message (chan, reason);
    }

  /* Update state and emit Closed signal */
  g_object_set (chan, "state", MUC_STATE_ENDED, NULL);

  tp_svc_channel_emit_closed (chan);
}

gboolean
_gabble_muc_channel_is_ready (GabbleMucChannel *chan)
{
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  return priv->ready_emitted;
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
  LmMessageNode *error_node;
  GabbleXmppError error;
  TpHandle actor = 0;
  guint reason_code = TP_CHANNEL_GROUP_CHANGE_REASON_NONE;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  if (strcmp (jid, priv->self_jid->str) != 0)
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

  error = gabble_xmpp_error_from_node (error_node);

  if (priv->state >= MUC_STATE_JOINED)
    {
      g_warning ("%s: presence error while already member of the channel -- NYI",
                 G_STRFUNC);
      return;
    }

  /* We're not a member, find out why the join request failed
   * and act accordingly. */
  if (error == XMPP_ERROR_NOT_AUTHORIZED)
    {
      /* channel can sit requiring a password indefinitely */
      clear_join_timer (chan);

      /* Password already provided and incorrect? */
      if (priv->state == MUC_STATE_AUTH)
        {
          provide_password_return_if_pending (chan, FALSE);

          return;
        }

      DEBUG ("password required to join, changing password flags");

      change_password_flags (chan, TP_CHANNEL_PASSWORD_FLAG_PROVIDE, 0);

      g_object_set (chan, "state", MUC_STATE_AUTH, NULL);
    }
  else
    {
      GError *tp_error;

      switch (error) {
        case XMPP_ERROR_FORBIDDEN:
          tp_error = g_error_new (TP_ERRORS, TP_ERROR_CHANNEL_BANNED,
                                  "banned from room");
          reason_code = TP_CHANNEL_GROUP_CHANGE_REASON_BANNED;
          break;
        case XMPP_ERROR_SERVICE_UNAVAILABLE:
          tp_error = g_error_new (TP_ERRORS, TP_ERROR_CHANNEL_FULL,
                                  "room is full");
          break;
        case XMPP_ERROR_REGISTRATION_REQUIRED:
          tp_error = g_error_new (TP_ERRORS, TP_ERROR_CHANNEL_INVITE_ONLY,
                                  "room is invite only");
          break;
        case XMPP_ERROR_CONFLICT:
          if (priv->nick_retry_count < MAX_NICK_RETRIES)
            {
              g_string_append_c (priv->self_jid, '_');

              if (send_join_request (chan, priv->password, &tp_error))
                {
                  priv->nick_retry_count++;
                  return;
                }
            }
          else
            {
              tp_error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                  "nickname already in use and retry count exceeded");
            }
          break;
        default:
          if (error != INVALID_XMPP_ERROR)
            {
              tp_error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                                      gabble_xmpp_error_description (error));
            }
          else
            {
              tp_error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                                      "unknown error");
            }
          break;
      }

      g_signal_emit (chan, signals[JOIN_ERROR], 0, tp_error);

      close_channel (chan, tp_error->message, FALSE, actor, reason_code);

      g_error_free (tp_error);
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

static LmMessageNode *
config_form_get_form_node (LmMessage *msg)
{
  LmMessageNode *node;

  /* find the query node */
  node = lm_message_node_get_child (msg->node, "query");
  if (node == NULL)
    return NULL;

  /* then the form node */
  for (node = node->children; node; node = node->next)
    {
      if (tp_strdiff (node->name, "x"))
        {
          continue;
        }

      if (!lm_message_node_has_namespace (node, NS_X_DATA, NULL))
        {
          continue;
        }

      if (tp_strdiff (lm_message_node_get_attribute (node, "type"), "form"))
        {
          continue;
        }

      return node;
    }

  return NULL;
}

static LmHandlerResult
perms_config_form_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                            LmMessage *reply_msg, GObject *object,
                            gpointer user_data)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);
  LmMessageNode *form_node, *node;

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      g_warning ("%s: request for config form denied, property permissions "
                 "will be inaccurate", G_STRFUNC);
      goto OUT;
    }

  /* just in case our affiliation has changed in the meantime */
  if (priv->self_affil != AFFILIATION_OWNER)
    goto OUT;

  form_node = config_form_get_form_node (reply_msg);
  if (form_node == NULL)
    {
      g_warning ("%s: form node node found, property permissions will be "
                 "inaccurate", G_STRFUNC);
      goto OUT;
    }

  for (node = form_node->children; node; node = node->next)
    {
      const gchar *var;

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
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);
  TpChannelGroupFlags grp_flags_add, grp_flags_rem;
  TpPropertyFlags prop_flags_add, prop_flags_rem;
  GArray *changed_props_val, *changed_props_flags;

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

  tp_group_mixin_change_flags ((TpSvcChannelInterfaceGroup *) chan,
      grp_flags_add, grp_flags_rem);


  /*
   * Update write capabilities based on room configuration
   * and own role and affiliation.
   */

  changed_props_val = g_array_sized_new (FALSE, FALSE, sizeof (guint),
      NUM_ROOM_PROPS);
  changed_props_flags = g_array_sized_new (FALSE, FALSE, sizeof (guint),
      NUM_ROOM_PROPS);

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

  /* Room definition */
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

  tp_properties_mixin_change_flags (G_OBJECT (chan),
      ROOM_PROP_SUBJECT, prop_flags_add, prop_flags_rem,
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
      node = lm_message_node_add_child (msg->node, "query", NULL);
      lm_message_node_set_attribute (node, "xmlns", NS_MUC_OWNER);

      success = _gabble_connection_send_with_reply (priv->conn, msg,
          perms_config_form_reply_cb, G_OBJECT (chan), NULL,
          &error);

      lm_message_unref (msg);

      if (!success)
        {
          g_warning ("%s: failed to request config form: %s",
              G_STRFUNC, error->message);
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
  g_array_free (changed_props_val, TRUE);
  g_array_free (changed_props_flags, TRUE);
}

/**
 * _gabble_muc_channel_member_presence_updated
 */
void
_gabble_muc_channel_member_presence_updated (GabbleMucChannel *chan,
                                             TpHandle handle,
                                             LmMessage *message,
                                             LmMessageNode *x_node)
{
  GabbleMucChannelPrivate *priv;
  TpBaseConnection *conn;
  TpIntSet *set;
  TpGroupMixin *mixin;
  LmMessageNode *item_node, *node;
  const gchar *affil, *role, *owner_jid, *status_code;
  TpHandle actor = 0;
  guint reason_code = TP_CHANNEL_GROUP_CHANGE_REASON_NONE;
  TpHandleRepoIface *contact_handles;

  DEBUG ("called");

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);
  conn = (TpBaseConnection *)priv->conn;
  contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  mixin = TP_GROUP_MIXIN (chan);

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
  owner_jid = lm_message_node_get_attribute (item_node, "jid");

  /* update channel members according to presence */
  set = tp_intset_new ();
  tp_intset_add (set, handle);

  if (lm_message_get_sub_type (message) != LM_MESSAGE_SUB_TYPE_UNAVAILABLE)
    {
      if (!tp_handle_set_is_member (mixin->members, handle))
        {
          tp_group_mixin_change_members ((TpSvcChannelInterfaceGroup *)chan, "", set, NULL,
                                             NULL, NULL, 0, 0);

          if (owner_jid != NULL)
            {
              TpHandle owner_handle;

              owner_handle = tp_handle_ensure (contact_handles, owner_jid,
                  GUINT_TO_POINTER (GABBLE_JID_GLOBAL), NULL);
              if (owner_handle == 0)
                {
                  DEBUG ("ignoring invalid owner JID %s in MUC presence",
                      owner_jid);
                }
              else
                {
                  tp_group_mixin_add_handle_owner (
                      (TpSvcChannelInterfaceGroup *)chan, handle,
                      owner_handle);
                  tp_handle_unref (contact_handles, owner_handle);
                }
            }

          if (handle == mixin->self_handle)
            {
              g_object_set (chan, "state", MUC_STATE_JOINED, NULL);
            }
        }

      if (handle == mixin->self_handle)
        {
          GabbleMucRole new_role;
          GabbleMucAffiliation new_affil;

          /* accept newly-created room settings before we send anything
           * below which queryies them. */
          if (status_code && strcmp (status_code, "201") == 0)
            {
              LmMessage *msg;
              GError *error = NULL;

              msg = lm_message_new_with_sub_type (priv->jid, LM_MESSAGE_TYPE_IQ,
                                                  LM_MESSAGE_SUB_TYPE_SET);

              node = lm_message_node_add_child (msg->node, "query", NULL);
              lm_message_node_set_attribute (node, "xmlns", NS_MUC_OWNER);

              node = lm_message_node_add_child (node, "x", NULL);
              lm_message_node_set_attributes (node,
                                              "xmlns", NS_X_DATA,
                                              "type", "submit",
                                              NULL);

              if (!_gabble_connection_send_with_reply (priv->conn, msg,
                    room_created_submit_reply_cb, G_OBJECT (chan), NULL, &error))
                {
                  g_warning ("%s: failed to send submit message: %s", G_STRFUNC,
                             error->message);
                  g_error_free (error);

                  close_channel (chan, NULL, TRUE, actor, reason_code);

                  goto OUT;
                }
            }

          /* Update room properties */
          room_properties_update (chan);

          /* update permissions after requesting new properties so that if we
           * become an owner, we get our configuration form reply after the
           * discovery reply, so we know whether there is a description
           * property before we try and decide whether we can write to it. */
          new_role = get_role_from_string (role);
          new_affil = get_affiliation_from_string (affil);

          if (new_role != priv->self_role || new_affil != priv->self_affil)
            {
              priv->self_role = new_role;
              priv->self_affil = new_affil;

              update_permissions (chan);
            }
        }
    }
  else
    {
      LmMessageNode *reason_node, *actor_node;
      const gchar *reason = "", *actor_jid = "";

      actor_node = lm_message_node_get_child (item_node, "actor");
      if (actor_node != NULL)
        {
          actor_jid = lm_message_node_get_attribute (actor_node, "jid");
          if (actor_jid != NULL)
            {
              actor = tp_handle_ensure (contact_handles, actor_jid, NULL,
                  NULL);
              if (actor == 0)
                {
                  DEBUG ("ignoring invalid actor JID %s", actor_jid);
                }
            }
        }

      /* Possible reasons we could have been removed from the room:
       * 301 banned
       * 307 kicked
       * 321 "because of an affiliation change" - no reason_code
       * 322 room has become members-only and we're not a member - no reason_code
       * 332 system (server) is being shut down - no reason code
       */
      if (status_code)
        {
          if (strcmp (status_code, "301") == 0)
            {
              reason_code = TP_CHANNEL_GROUP_CHANGE_REASON_BANNED;
            }
          else if (strcmp (status_code, "307") == 0)
            {
              reason_code = TP_CHANNEL_GROUP_CHANGE_REASON_KICKED;
            }
        }

      reason_node = lm_message_node_get_child (item_node, "reason");
      if (reason_node != NULL)
        {
          reason = lm_message_node_get_value (reason_node);
        }

      if (handle != mixin->self_handle)
        {
          tp_group_mixin_change_members ((TpSvcChannelInterfaceGroup *)chan, reason,
                                             NULL, set, NULL, NULL,
                                             actor, reason_code);
        }
      else
        {
          close_channel (chan, reason, FALSE, actor, reason_code);
        }
    }

OUT:
  tp_intset_destroy (set);
  if (actor)
    tp_handle_unref (contact_handles, actor);
}


/**
 * _gabble_muc_channel_receive
 */
gboolean
_gabble_muc_channel_receive (GabbleMucChannel *chan,
                             TpChannelTextMessageType msg_type,
                             TpHandleType handle_type,
                             TpHandle sender,
                             time_t timestamp,
                             const gchar *text,
                             LmMessage *msg)
{
  gboolean error;
  GabbleMucChannelPrivate *priv;
  LmMessageNode *subj_node, *node;
  GValue val = { 0, };

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  error = lm_message_get_sub_type (msg) == LM_MESSAGE_SUB_TYPE_ERROR;

  subj_node = lm_message_node_get_child (msg->node, "subject");

  if (subj_node)
    {
      GArray *changed_values, *changed_flags;

      if (priv->properties_ctx)
        {
          tp_properties_context_remove (priv->properties_ctx,
              ROOM_PROP_SUBJECT);
        }

      if (error)
        {
          GabbleXmppError xmpp_error = INVALID_XMPP_ERROR;
          const gchar *err_desc = NULL;

          node = lm_message_node_get_child (msg->node, "error");
          if (node)
            {
              xmpp_error = gabble_xmpp_error_from_node (node);
            }

          if (xmpp_error != INVALID_XMPP_ERROR)
            {
              err_desc = gabble_xmpp_error_description (xmpp_error);
            }

          if (priv->properties_ctx)
            {
              GError *error = NULL;

              error = g_error_new (TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
                  (err_desc) ? err_desc : "failed to change subject");

              tp_properties_context_return (priv->properties_ctx, error);
              priv->properties_ctx = NULL;

              /* Get the properties into a consistent state. */
              room_properties_update (chan);
            }

          return TRUE;
        }

      changed_values = g_array_sized_new (FALSE, FALSE, sizeof (guint),
          NUM_ROOM_PROPS);
      changed_flags = g_array_sized_new (FALSE, FALSE, sizeof (guint),
          NUM_ROOM_PROPS);

      /* ROOM_PROP_SUBJECT */
      g_value_init (&val, G_TYPE_STRING);
      g_value_set_string (&val, lm_message_node_get_value (subj_node));

      tp_properties_mixin_change_value (G_OBJECT (chan),
          ROOM_PROP_SUBJECT, &val, changed_values);

      tp_properties_mixin_change_flags (G_OBJECT (chan),
          ROOM_PROP_SUBJECT, TP_PROPERTY_FLAG_READ, 0,
          changed_flags);

      g_value_unset (&val);

      if (handle_type == TP_HANDLE_TYPE_CONTACT)
        {
          /* ROOM_PROP_SUBJECT_CONTACT */
          g_value_init (&val, G_TYPE_UINT);
          g_value_set_uint (&val, sender);

          tp_properties_mixin_change_value (G_OBJECT (chan),
              ROOM_PROP_SUBJECT_CONTACT, &val, changed_values);

          tp_properties_mixin_change_flags (G_OBJECT (chan),
              ROOM_PROP_SUBJECT_CONTACT, TP_PROPERTY_FLAG_READ, 0,
              changed_flags);

          g_value_unset (&val);
        }

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
      g_array_free (changed_values, TRUE);
      g_array_free (changed_flags, TRUE);

      if (priv->properties_ctx)
        {
          if (tp_properties_context_return_if_done (priv->properties_ctx))
            {
              priv->properties_ctx = NULL;
            }
        }

      return TRUE;
    }
  else if (handle_type == TP_HANDLE_TYPE_ROOM)
    {
      NODE_DEBUG (msg->node, "ignoring message from channel");

      return TRUE;
    }
  else if ((sender == chan->group.self_handle) && (timestamp == 0))
    {
      /* If we sent the message and it's not delayed, just emit the sent signal */
      timestamp = time (NULL);
      tp_svc_channel_type_text_emit_sent (chan, timestamp, msg_type, text);

      return TRUE;
    }

  /* Receive messages from other contacts and our own if they're delayed, and
   * set the timestamp for non-delayed messages */
  if (timestamp == 0)
      timestamp = time (NULL);

  return tp_text_mixin_receive (G_OBJECT (chan), msg_type, sender,
      timestamp, text);
}

void
_gabble_muc_channel_handle_invited (GabbleMucChannel *chan,
                                    TpHandle inviter,
                                    const gchar *message)
{
  GabbleMucChannelPrivate *priv;
  TpBaseConnection *conn;
  TpHandle self_handle;
  TpIntSet *set_members, *set_pending;
  TpHandleRepoIface *contact_handles;

  g_assert (GABBLE_IS_MUC_CHANNEL (chan));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);
  conn = (TpBaseConnection *)priv->conn;
  contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  /* add ourself to local pending and the inviter to members */
  set_members = tp_intset_new ();
  set_pending = tp_intset_new ();

  tp_intset_add (set_members, inviter);

  /* get our own identity in the room */
  contact_handle_to_room_identity (chan, conn->self_handle,
                                   &self_handle, &priv->self_jid);
  tp_intset_add (set_pending, self_handle);

  tp_group_mixin_change_members ((TpSvcChannelInterfaceGroup *)chan, message, set_members,
                                     NULL, set_pending, NULL, inviter,
                                     TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);

  tp_intset_destroy (set_members);
  tp_intset_destroy (set_pending);
  tp_handle_unref (contact_handles, self_handle);

  /* queue the message */
  if (message && (message[0] != '\0'))
    {
      tp_text_mixin_receive (G_OBJECT (chan), TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE, inviter,
                             time(NULL), message);
    }

  /* emit READY signal so NewChannel is emitted */
  g_signal_emit (chan, signals[READY], 0);
  priv->ready_emitted = TRUE;
}

/**
 * _gabble_muc_channel_state_receive
 *			      
 * Send the D-BUS signal ChatStateChanged
 * on org.freedesktop.Telepathy.Channel.Interface.ChatState
 */
void
_gabble_muc_channel_state_receive (GabbleMucChannel *chan, guint state, guint from_handle)
{
  GabbleMucChannelPrivate *priv;

  g_assert (state < NUM_TP_CHANNEL_CHAT_STATES);
  g_assert (GABBLE_IS_MUC_CHANNEL (chan));
  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  tp_svc_channel_interface_chat_state_emit_chat_state_changed ((TpSvcChannelInterfaceChatState*)chan,
      from_handle, state);
}

/**
 * gabble_muc_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_muc_channel_close (TpSvcChannel *iface,
                          DBusGMethodInvocation *context)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (iface);
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (self));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);

  DEBUG ("called on %p", self);

  if (priv->closed)
    {
      GError already = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Channel already closed"};
      DEBUG ("channel already closed");

      dbus_g_method_return_error (context, &already);
      return;
    }

  close_channel (self, NULL, TRUE, 0, 0);

  tp_svc_channel_return_from_close (context);
}


/**
 * gabble_muc_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_muc_channel_get_channel_type (TpSvcChannel *iface,
                                     DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_TEXT);
}


/**
 * gabble_muc_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_muc_channel_get_handle (TpSvcChannel *iface,
                               DBusGMethodInvocation *context)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (iface);
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (self));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);

  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_ROOM,
      priv->handle);
}


/**
 * gabble_muc_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_muc_channel_get_interfaces (TpSvcChannel *iface,
                                   DBusGMethodInvocation *context)
{
  const gchar *interfaces[] = {
      TP_IFACE_CHANNEL_INTERFACE_GROUP,
      TP_IFACE_CHANNEL_INTERFACE_PASSWORD,
      TP_IFACE_PROPERTIES_INTERFACE,
      TP_IFACE_CHANNEL_INTERFACE_CHAT_STATE,
      NULL
  };

  tp_svc_channel_return_from_get_interfaces (context, interfaces);
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

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);

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

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);

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
 * Implements D-Bus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 */
static void
gabble_muc_channel_send (TpSvcChannelTypeText *iface,
                         guint type,
                         const gchar *text,
                         DBusGMethodInvocation *context)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (iface);
  GabbleMucChannelPrivate *priv;
  GError *error = NULL;

  g_assert (GABBLE_IS_MUC_CHANNEL (self));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);

  if (!gabble_text_mixin_send (G_OBJECT (self), type,
      LM_MESSAGE_SUB_TYPE_GROUPCHAT, TP_CHANNEL_CHAT_STATE_ACTIVE, priv->jid,
      text, priv->conn, FALSE /* emit_signal */, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return;
    }

  tp_svc_channel_type_text_return_from_send (context);
}


static gboolean
gabble_muc_channel_add_member (TpSvcChannelInterfaceGroup *obj, TpHandle handle, const gchar *message, GError **error)
{
  GabbleMucChannelPrivate *priv;
  TpGroupMixin *mixin;
  const gchar *jid;
  LmMessage *msg;
  LmMessageNode *x_node, *invite_node;
  gboolean result;

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (GABBLE_MUC_CHANNEL (obj));

  mixin = TP_GROUP_MIXIN (obj);

  if (handle == mixin->self_handle)
    {
      TpIntSet *set_empty, *set_members, *set_pending;
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
      set_empty = tp_intset_new ();
      set_members = tp_intset_new ();
      set_pending = tp_intset_new ();

      arr_members = tp_handle_set_to_array (mixin->members);
      if (arr_members->len > 0)
        {
          tp_intset_add (set_members, g_array_index (arr_members, guint32, 0));
        }
      g_array_free (arr_members, TRUE);

      tp_intset_add (set_pending, handle);

      tp_group_mixin_change_members (obj, "", set_empty, set_members,
                                         set_empty, set_pending, 0,
                                         TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);

      tp_intset_destroy (set_empty);
      tp_intset_destroy (set_members);
      tp_intset_destroy (set_pending);

      /* seek to enter the room */
      result = send_join_request (GABBLE_MUC_CHANNEL (obj), NULL, error);

      g_object_set (obj, "state",
                    (result) ? MUC_STATE_INITIATED : MUC_STATE_ENDED,
                    NULL);

      /* deny adding */
      tp_group_mixin_change_flags (obj, 0, TP_CHANNEL_GROUP_FLAG_CAN_ADD);

      /* clear message queue (which might contain an invite reason) */
      tp_text_mixin_clear (G_OBJECT (obj));

      return result;
    }

  /* check that we're indeed a member when attempting to invite others */
  if (priv->state < MUC_STATE_JOINED)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "channel membership is required for inviting others");

      return FALSE;
    }

  msg = lm_message_new (priv->jid, LM_MESSAGE_TYPE_MESSAGE);

  x_node = lm_message_node_add_child (msg->node, "x", NULL);
  lm_message_node_set_attribute (x_node, "xmlns", NS_MUC_USER);

  invite_node = lm_message_node_add_child (x_node, "invite", NULL);

  jid = tp_handle_inspect (TP_GROUP_MIXIN (obj)->handle_repo, handle);

  lm_message_node_set_attribute (invite_node, "to", jid);

  if (*message != '\0')
    {
      lm_message_node_add_child (invite_node, "reason", message);
    }

  NODE_DEBUG (msg->node, "sending MUC invitation");

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
gabble_muc_channel_remove_member (TpSvcChannelInterfaceGroup *obj, TpHandle handle, const gchar *message, GError **error)
{
  GabbleMucChannelPrivate *priv;
  LmMessage *msg;
  LmMessageNode *query_node, *item_node;
  const gchar *jid;
  gchar *nick;
  gboolean result;

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (GABBLE_MUC_CHANNEL (obj));

  msg = lm_message_new_with_sub_type (priv->jid, LM_MESSAGE_TYPE_IQ,
                                      LM_MESSAGE_SUB_TYPE_SET);

  query_node = lm_message_node_add_child (msg->node, "query", NULL);
  lm_message_node_set_attribute (query_node, "xmlns", NS_MUC_ADMIN);

  item_node = lm_message_node_add_child (query_node, "item", NULL);

  jid = tp_handle_inspect (TP_GROUP_MIXIN (obj)->handle_repo, handle);

  gabble_decode_jid (jid, NULL, NULL, &nick);

  lm_message_node_set_attributes (item_node,
                                  "nick", nick,
                                  "role", "none",
                                  NULL);

  g_free (nick);

  if (*message != '\0')
    {
      lm_message_node_add_child (item_node, "reason", message);
    }

  NODE_DEBUG (msg->node, "sending MUC kick request");

  result = _gabble_connection_send_with_reply (priv->conn, msg,
                                               kick_request_reply_cb,
                                               (GObject *)obj, (gpointer) jid,
                                               error);

  lm_message_unref (msg);

  return result;
}


static LmHandlerResult request_config_form_reply_cb (GabbleConnection *conn, LmMessage *sent_msg, LmMessage *reply_msg, GObject *object, gpointer user_data);

static gboolean
gabble_muc_channel_do_set_properties (GObject *obj, TpPropertiesContext *ctx, GError **error)
{
  GabbleMucChannelPrivate *priv;
  LmMessage *msg;
  LmMessageNode *node;
  gboolean success;

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (GABBLE_MUC_CHANNEL (obj));

  g_assert (priv->properties_ctx == NULL);

  /* Changing subject? */
  if (tp_properties_context_has (ctx, ROOM_PROP_SUBJECT))
    {
      const gchar *str;

      str = g_value_get_string (tp_properties_context_get (ctx, ROOM_PROP_SUBJECT));

      msg = lm_message_new_with_sub_type (priv->jid,
          LM_MESSAGE_TYPE_MESSAGE, LM_MESSAGE_SUB_TYPE_GROUPCHAT);
      lm_message_node_add_child (msg->node, "subject", str);

      success = _gabble_connection_send (priv->conn, msg, error);

      lm_message_unref (msg);

      if (!success)
        return FALSE;
    }

  /* Changing any other properties? */
  if (tp_properties_context_has_other_than (ctx, ROOM_PROP_SUBJECT))
    {
      msg = lm_message_new_with_sub_type (priv->jid,
          LM_MESSAGE_TYPE_IQ, LM_MESSAGE_SUB_TYPE_GET);
      node = lm_message_node_add_child (msg->node, "query", NULL);
      lm_message_node_set_attribute (node, "xmlns", NS_MUC_OWNER);

      success = _gabble_connection_send_with_reply (priv->conn, msg,
          request_config_form_reply_cb, G_OBJECT (obj), NULL,
          error);

      lm_message_unref (msg);

      if (!success)
        return FALSE;
    }

  priv->properties_ctx = ctx;

  return TRUE;
}

static LmHandlerResult request_config_form_submit_reply_cb (GabbleConnection *conn, LmMessage *sent_msg, LmMessage *reply_msg, GObject *object, gpointer user_data);

static LmHandlerResult
request_config_form_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                              LmMessage *reply_msg, GObject *object,
                              gpointer user_data)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);
  TpPropertiesContext *ctx = priv->properties_ctx;
  GError *error = NULL;
  LmMessage *msg = NULL;
  LmMessageNode *submit_node, *form_node, *node;
  guint i, props_left;

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

  node = lm_message_node_add_child (msg->node, "query", NULL);
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
          DEBUG ("skipping node '%s'", node->name);
          continue;
        }

      var = lm_message_node_get_attribute (node, "var");
      if (var == NULL) {
        DEBUG ("skipping node '%s' because of lacking var attribute",
               node->name);
        continue;
      }

      value_node = lm_message_node_get_child (node, "value");
      if (value_node == NULL)
        {
          DEBUG ("skipping var '%s' because of lacking value attribute",
                 var);
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
          g_warning ("%s: ignoring field '%s'", G_STRFUNC, var);
          continue;
        }

      DEBUG ("looking up %s", room_property_signatures[id].name);

      if (!tp_properties_context_has (ctx, id))
        continue;

      if (!val_str)
        {
          const GValue *provided_value;

          provided_value = tp_properties_context_get (ctx, id);

          switch (type) {
            case G_TYPE_BOOLEAN:
              val_bool = g_value_get_boolean (provided_value);
              sprintf (buf, "%d", (invert) ? !val_bool : val_bool);
              val_str = buf;
              break;
            case G_TYPE_STRING:
              val_str = g_value_get_string (provided_value);
              break;
            default:
              g_assert_not_reached ();
          }
        }

      lm_message_node_set_value (value_node, val_str);

      props_left &= ~(1 << id);
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

  _gabble_connection_send_with_reply (priv->conn, msg,
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
request_config_form_submit_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                                     LmMessage *reply_msg, GObject *object,
                                     gpointer user_data)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);
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
  GabbleMucChannelPrivate *priv;
  GError *error = NULL;

  g_assert (GABBLE_IS_MUC_CHANNEL (self));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);

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

  if (error != NULL || !gabble_text_mixin_send (G_OBJECT (self),
        TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE, LM_MESSAGE_SUB_TYPE_GROUPCHAT,
        state, priv->jid, NULL, priv->conn, FALSE /* emit_signal */, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return;
    }

  tp_svc_channel_interface_chat_state_return_from_set_chat_state (context);
}

static void
channel_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, gabble_muc_channel_##x)
  IMPLEMENT(close);
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}

static void
text_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelTypeTextClass *klass = (TpSvcChannelTypeTextClass *)g_iface;

  tp_text_mixin_iface_init (g_iface, iface_data);
#define IMPLEMENT(x) tp_svc_channel_type_text_implement_##x (\
    klass, gabble_muc_channel_##x)
  IMPLEMENT(send);
#undef IMPLEMENT
}

static void
password_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfacePasswordClass *klass = (TpSvcChannelInterfacePasswordClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_password_implement_##x (\
    klass, gabble_muc_channel_##x)
  IMPLEMENT(get_password_flags);
  IMPLEMENT(provide_password);
#undef IMPLEMENT
}

static void
chat_state_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceChatStateClass *klass = (TpSvcChannelInterfaceChatStateClass *)g_iface;
#define IMPLEMENT(x) tp_svc_channel_interface_chat_state_implement_##x (\
    klass, gabble_muc_channel_##x)
  IMPLEMENT(set_chat_state);
#undef IMPLEMENT
}
