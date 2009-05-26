/*
 * muc-factory.c - Source for GabbleMucFactory
 * Copyright (C) 2006 Collabora Ltd.
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
#include "muc-factory.h"

#define DBUS_API_SUBJECT_TO_CHANGE

#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG GABBLE_DEBUG_MUC

#include "caps-channel-manager.h"
#include "connection.h"
#include "conn-olpc.h"
#include "debug.h"
#include "disco.h"
#include "message-util.h"
#include "muc-channel.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "tubes-channel.h"
#include "tube-dbus.h"
#include "tube-stream.h"
#include "util.h"

static void channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleMucFactory, gabble_muc_factory, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER, NULL));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

typedef struct _GabbleMucFactoryPrivate GabbleMucFactoryPrivate;
struct _GabbleMucFactoryPrivate
{
  GabbleConnection *conn;
  gulong status_changed_id;

  LmMessageHandler *message_cb;
  LmMessageHandler *presence_cb;
  /* GUINT_TO_POINTER(room_handle) => (GabbleMucChannel *) */
  GHashTable *text_channels;
  /* GUINT_TO_POINTER(room_handle) => (GabbleTubesChannel *) */
  GHashTable *tubes_channels;
  /* Tubes channels which will be considered ready when the corresponding
   * text channel is created.
   * Borrowed GabbleMucChannel => borrowed GabbleTubesChannel */
  GHashTable *text_needed_for_tubes;
  /* Tube channels which will be considered ready when the corresponding
   * tubes channel is created.
   * Borrowed GabbleTubesChannel => GSlist of borrowed GabbleTubeIface */
  GHashTable *tubes_needed_for_tube;
  /* GabbleDiscoRequest * => NULL (used as a set) */
  GHashTable *disco_requests;

  /* Map from channels to the request-tokens of requests that they will satisfy
   * when they're ready.
   * Borrowed TpExportableChannel => GSList of gpointer */
  GHashTable *queued_requests;

  gboolean dispose_has_run;
};

#define GABBLE_MUC_FACTORY_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MUC_FACTORY, \
                                GabbleMucFactoryPrivate))

static GObject *gabble_muc_factory_constructor (GType type, guint n_props,
    GObjectConstructParam *props);

static void
gabble_muc_factory_init (GabbleMucFactory *fac)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  priv->text_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
  priv->tubes_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
  priv->text_needed_for_tubes = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, NULL);
  priv->tubes_needed_for_tube = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) g_slist_free);

  priv->disco_requests = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                NULL, NULL);

  priv->queued_requests = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, NULL);

  priv->message_cb = NULL;
  priv->presence_cb = NULL;

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
}


static void
cancel_disco_request (gpointer key, gpointer value, gpointer user_data)
{
  GabbleDisco *disco = GABBLE_DISCO (user_data);
  GabbleDiscoRequest *request = (GabbleDiscoRequest *) key;

  gabble_disco_cancel_request (disco, request);
}


static void gabble_muc_factory_close_all (GabbleMucFactory *fac);


static void
gabble_muc_factory_dispose (GObject *object)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (object);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  gabble_muc_factory_close_all (fac);
  g_assert (priv->text_channels == NULL);
  g_assert (priv->tubes_channels == NULL);
  g_assert (priv->text_needed_for_tubes == NULL);
  g_assert (priv->tubes_needed_for_tube == NULL);
  g_assert (priv->queued_requests == NULL);

  g_hash_table_foreach (priv->disco_requests, cancel_disco_request,
      priv->conn->disco);
  g_hash_table_destroy (priv->disco_requests);

  if (G_OBJECT_CLASS (gabble_muc_factory_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_muc_factory_parent_class)->dispose (object);
}

static void
gabble_muc_factory_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (object);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_muc_factory_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (object);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_muc_factory_class_init (GabbleMucFactoryClass *gabble_muc_factory_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_muc_factory_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_muc_factory_class,
      sizeof (GabbleMucFactoryPrivate));

  object_class->constructor = gabble_muc_factory_constructor;
  object_class->dispose = gabble_muc_factory_dispose;

  object_class->get_property = gabble_muc_factory_get_property;
  object_class->set_property = gabble_muc_factory_set_property;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this MUC factory object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}

/**
 * muc_channel_closed_cb:
 *
 * Signal callback for when a MUC channel is closed. Removes the references
 * that MucFactory holds to them.
 */
static void
muc_channel_closed_cb (GabbleMucChannel *chan, gpointer user_data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (user_data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpHandle room_handle;

  tp_channel_manager_emit_channel_closed_for_object (fac,
      TP_EXPORTABLE_CHANNEL (chan));

  if (priv->text_channels != NULL)
    {
      g_object_get (chan, "handle", &room_handle, NULL);

      DEBUG ("removing MUC channel with handle %d", room_handle);

      if (priv->tubes_channels != NULL)
        {
          GabbleTubesChannel *tubes;

          tubes = g_hash_table_lookup (priv->tubes_channels,
              GUINT_TO_POINTER (room_handle));
          if (tubes != NULL)
            gabble_tubes_channel_close (tubes);
        }

      g_hash_table_remove (priv->text_channels,
          GUINT_TO_POINTER (room_handle));
    }
}

/**
 * tubes_channel_closed_cb:
 *
 * Signal callback for when a tubes channel is closed. Removes the references
 * that MucFactory holds to them.
 */
static void
tubes_channel_closed_cb (GabbleTubesChannel *chan, gpointer user_data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (user_data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpHandle room_handle;

  tp_channel_manager_emit_channel_closed_for_object (fac,
      TP_EXPORTABLE_CHANNEL (chan));

  if (priv->tubes_channels != NULL)
    {
      g_object_get (chan, "handle", &room_handle, NULL);

      DEBUG ("removing MUC tubes channel with handle %d", room_handle);

      g_hash_table_remove (priv->tubes_channels,
          GUINT_TO_POINTER (room_handle));
    }
}


static void
muc_ready_cb (GabbleMucChannel *text_chan,
              gpointer data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  GabbleTubesChannel *tubes_chan;
  GSList *requests_satisfied_text, *requests_satisfied_tubes = NULL;
  gboolean text_requested;
  GSList *tube_channels, *l;

  DEBUG ("text chan=%p", text_chan);

  g_object_get (text_chan, "requested", &text_requested, NULL);

  requests_satisfied_text = g_hash_table_lookup (
      priv->queued_requests, text_chan);
  g_hash_table_steal (priv->queued_requests, text_chan);
  requests_satisfied_text = g_slist_reverse (requests_satisfied_text);

  tubes_chan = g_hash_table_lookup (priv->text_needed_for_tubes, text_chan);
  g_hash_table_remove (priv->text_needed_for_tubes, text_chan);

  if (tubes_chan != NULL)
    {
      requests_satisfied_tubes = g_hash_table_lookup (
          priv->queued_requests, tubes_chan);
      g_hash_table_steal (priv->queued_requests, tubes_chan);
    }

  if (tubes_chan == NULL || text_requested)
    {
      /* There is no tubes channel or the text channel has been explicitely
       * requested. In both cases, the text channel has to be announced
       * separately. */

      /* announce text channel */
      tp_channel_manager_emit_new_channel (fac,
          TP_EXPORTABLE_CHANNEL (text_chan), requests_satisfied_text);

      if (tubes_chan != NULL)
        {
          tp_channel_manager_emit_new_channel (fac,
              TP_EXPORTABLE_CHANNEL (tubes_chan), requests_satisfied_tubes);
        }
    }
  else
    {
      /* Announce text and tubes text_chan together */
      GHashTable *channels;

      channels = g_hash_table_new (g_direct_hash, g_direct_equal);
      g_hash_table_insert (channels, text_chan, requests_satisfied_text);
      g_hash_table_insert (channels, tubes_chan, requests_satisfied_tubes);

      tp_channel_manager_emit_new_channels (fac, channels);

      g_hash_table_destroy (channels);
    }

  /* Announce tube channels now */
  /* FIXME: we should probably aggregate tube announcement with tubes and text
   * ones in some cases. */
  tube_channels = g_hash_table_lookup (priv->tubes_needed_for_tube,
      tubes_chan);

  tube_channels = g_slist_reverse (tube_channels);
  for (l = tube_channels; l != NULL; l = g_slist_next (l))
    {
      GabbleTubeIface *tube_chan = GABBLE_TUBE_IFACE (l->data);
      GSList *requests_satisfied_tube;

      requests_satisfied_tube = g_hash_table_lookup (priv->queued_requests,
          tube_chan);
      g_hash_table_steal (priv->queued_requests, tube_chan);
      requests_satisfied_tube = g_slist_reverse (requests_satisfied_tube);

      tp_channel_manager_emit_new_channel (fac,
          TP_EXPORTABLE_CHANNEL (tube_chan), requests_satisfied_tube);

      g_slist_free (requests_satisfied_tube);
    }

  g_hash_table_remove (priv->tubes_needed_for_tube, tubes_chan);
  g_slist_free (requests_satisfied_text);
  g_slist_free (requests_satisfied_tubes);
}

static void
muc_join_error_cb (GabbleMucChannel *chan,
                   GError *error,
                   gpointer data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  GabbleTubesChannel *tubes_chan;
  GSList *requests_satisfied;
  GSList *iter;

  DEBUG ("error->code=%u, error->message=\"%s\"", error->code, error->message);

  requests_satisfied = g_slist_reverse (g_hash_table_lookup (
        priv->queued_requests, chan));
  g_hash_table_steal (priv->queued_requests, chan);

  for (iter = requests_satisfied; iter != NULL; iter = iter->next)
    {
      tp_channel_manager_emit_request_failed (fac, iter->data,
          error->domain, error->code, error->message);
    }

  g_slist_free (requests_satisfied);

  tubes_chan = g_hash_table_lookup (priv->text_needed_for_tubes, chan);

  if (tubes_chan != NULL)
    {
      g_hash_table_remove (priv->text_needed_for_tubes, chan);

      requests_satisfied = g_slist_reverse (g_hash_table_lookup (
            priv->queued_requests, tubes_chan));
      g_hash_table_steal (priv->queued_requests, tubes_chan);

      for (iter = requests_satisfied; iter != NULL; iter = iter->next)
        {
          tp_channel_manager_emit_request_failed (fac, iter->data,
              error->domain, error->code, error->message);
        }

      g_slist_free (requests_satisfied);
    }
}

/**
 * new_muc_channel
 */
static GabbleMucChannel *
new_muc_channel (GabbleMucFactory *fac,
                 TpHandle handle,
                 gboolean invited,
                 TpHandle inviter,
                 const gchar *message,
                 gboolean requested)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  GabbleMucChannel *chan;
  char *object_path;

  g_assert (g_hash_table_lookup (priv->text_channels,
        GUINT_TO_POINTER (handle)) == NULL);

  object_path = g_strdup_printf ("%s/MucChannel%u",
      conn->object_path, handle);

  DEBUG ("creating new chan, object path %s", object_path);

  chan = g_object_new (GABBLE_TYPE_MUC_CHANNEL,
       "connection", priv->conn,
       "object-path", object_path,
       "handle", handle,
       "invited", invited,
       "initiator-handle", invited ? inviter : conn->self_handle,
       "invitation-message", message,
       "requested", requested,
       NULL);

  g_signal_connect (chan, "closed", (GCallback) muc_channel_closed_cb, fac);

  g_hash_table_insert (priv->text_channels, GUINT_TO_POINTER (handle), chan);

  g_free (object_path);

  if (_gabble_muc_channel_is_ready (chan))
    muc_ready_cb (chan, fac);
  else
    g_signal_connect (chan, "ready", G_CALLBACK (muc_ready_cb), fac);

  g_signal_connect (chan, "join-error", G_CALLBACK (muc_join_error_cb),
                    fac);

  return chan;
}

/**
 * new_tubes_channel:
 *
 * Creates the GabbleTubesChannel object with the given parameters.
 */
static GabbleTubesChannel *
new_tubes_channel (GabbleMucFactory *fac,
                   TpHandle room,
                   GabbleMucChannel *muc,
                   TpHandle initiator,
                   gboolean requested)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  GabbleTubesChannel *chan;
  char *object_path;

  g_assert (g_hash_table_lookup (priv->tubes_channels,
        GUINT_TO_POINTER (room)) == NULL);

  object_path = g_strdup_printf ("%s/MucTubesChannel%u",
      conn->object_path, room);

  DEBUG ("creating new tubes chan, object path %s", object_path);

  chan = g_object_new (GABBLE_TYPE_TUBES_CHANNEL,
      "connection", priv->conn,
      "object-path", object_path,
      "handle", room,
      "handle-type", TP_HANDLE_TYPE_ROOM,
      "muc", muc,
      "initiator-handle", initiator,
      "requested", requested,
      NULL);

  g_signal_connect (chan, "closed", (GCallback) tubes_channel_closed_cb, fac);

  g_hash_table_insert (priv->tubes_channels, GUINT_TO_POINTER (room), chan);

  g_free (object_path);

  return chan;
}

static void
do_invite (GabbleMucFactory *fac,
           const gchar *room,
           TpHandle inviter_handle,
           const gchar *reason)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);
  TpHandle room_handle;

  room_handle = tp_handle_ensure (room_repo, room, NULL, NULL);

  if (room_handle == 0)
    {
      DEBUG ("got a MUC invitation message with invalid room JID \"%s\"; "
          "ignoring", room);
      return;
    }

  if (g_hash_table_lookup (priv->text_channels,
        GUINT_TO_POINTER (room_handle)) == NULL)
    {
      new_muc_channel (fac, room_handle, TRUE, inviter_handle, reason, FALSE);
    }
  else
    {
      DEBUG ("ignoring invite to room \"%s\"; we're already there", room);
    }

  tp_handle_unref (room_repo, room_handle);
}

struct DiscoInviteData {
    GabbleMucFactory *factory;
    gchar *reason;
    TpHandle inviter;
};

/**
 * obsolete_invite_disco_cb:
 *
 * Callback for disco request we fired upon encountering obsolete disco.
 * If the object is in fact MUC room, create a channel for it.
 */
static void
obsolete_invite_disco_cb (GabbleDisco *self,
                          GabbleDiscoRequest *request,
                          const gchar *jid,
                          const gchar *node,
                          LmMessageNode *query_result,
                          GError* error,
                          gpointer user_data)
{
  struct DiscoInviteData *data = (struct DiscoInviteData *) user_data;

  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (data->factory);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *identity;
  const char *category = NULL, *type = NULL;

  g_hash_table_remove (priv->disco_requests, request);

  if (error != NULL)
    {
      DEBUG ("ignoring obsolete invite to room '%s'; got disco error: %s",
          jid, error->message);
      goto out;
    }

  identity = lm_message_node_get_child (query_result, "identity");
  if (identity != NULL)
    {
      category = lm_message_node_get_attribute (identity, "category");
      type = lm_message_node_get_attribute (identity, "type");
    }

  if (tp_strdiff (category, "conference") ||
      tp_strdiff (type, "text"))
    {
      DEBUG ("obsolete invite request specified inappropriate jid '%s' "
          "(not a text conference); ignoring request", jid);
      goto out;
    }

  /* OK, it's MUC after all, create a new channel */
  do_invite (fac, jid, data->inviter, data->reason);

out:
  tp_handle_unref (contact_repo, data->inviter);
  g_free (data->reason);
  g_slice_free (struct DiscoInviteData, data);
}

static gboolean
process_muc_invite (GabbleMucFactory *fac,
                    LmMessage *message,
                    const gchar *from,
                    TpChannelTextSendError send_error)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  LmMessageNode *x_node, *invite_node, *reason_node;
  const gchar *invite_from, *reason = NULL;
  TpHandle inviter_handle;
  gchar *room;

  /* does it have a muc subnode? */
  x_node = lm_message_node_get_child_with_namespace (message->node, "x",
      NS_MUC_USER);

  if (x_node == NULL)
    return FALSE;

  /* and an invitation? */
  invite_node = lm_message_node_get_child (x_node, "invite");

  if (invite_node == NULL)
    return FALSE;

  /* FIXME: do something with these? */
  if (send_error != GABBLE_TEXT_CHANNEL_SEND_NO_ERROR)
    {
      NODE_DEBUG (message->node, "got a MUC invitation message with a send "
          "error; ignoring");

      return TRUE;
    }

  invite_from = lm_message_node_get_attribute (invite_node, "from");
  if (invite_from == NULL)
    {
      NODE_DEBUG (message->node, "got a MUC invitation message with no JID; "
          "ignoring");

      return TRUE;
    }

  inviter_handle = tp_handle_ensure (contact_repo, invite_from,
      NULL, NULL);
  if (inviter_handle == 0)
    {
      NODE_DEBUG (message->node, "got a MUC invitation message with invalid "
          "inviter JID; ignoring");

      return TRUE;
    }

  reason_node = lm_message_node_get_child (invite_node, "reason");

  if (reason_node != NULL)
    reason = lm_message_node_get_value (reason_node);

  /* create the channel */
  room = gabble_remove_resource (from);
  do_invite (fac, room, inviter_handle, reason);
  g_free (room);

  tp_handle_unref (contact_repo, inviter_handle);

  return TRUE;
}

static gboolean
process_obsolete_invite (GabbleMucFactory *fac,
                         LmMessage *message,
                         const gchar *from,
                         const gchar *body,
                         TpChannelTextSendError send_error)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  LmMessageNode *x_node;
  const gchar *room;
  TpHandle inviter_handle;
  GabbleDiscoRequest *request;
  struct DiscoInviteData *disco_udata;

  /* check for obsolete invite method */
  x_node = lm_message_node_get_child_with_namespace (message->node, "x",
      NS_X_CONFERENCE);
  if (x_node == NULL)
    return FALSE;

  /* this can only happen if the user sent an obsolete invite with another
   * client or something */
  if (send_error != GABBLE_TEXT_CHANNEL_SEND_NO_ERROR)
    {
      NODE_DEBUG (message->node, "got an obsolete MUC invitation message with "
          "a send error; ignoring");

      return TRUE;
    }

  /* the room JID is in x */
  room = lm_message_node_get_attribute (x_node, "jid");
  if (room == NULL)
    {
      NODE_DEBUG (message->node, "got a obsolete MUC invitation with no room "
          "JID; ignoring");

      return TRUE;
    }

  /* the inviter JID is in "from" */
  inviter_handle = tp_handle_ensure (contact_repo, from, NULL, NULL);
  if (inviter_handle == 0)
    {
      NODE_DEBUG (message->node, "got an obsolete MUC invitation message from "
          "an invalid JID; ignoring");

      return TRUE;
    }

  disco_udata = g_slice_new0 (struct DiscoInviteData);
  disco_udata->factory = fac;
  disco_udata->reason = g_strdup (body);
  disco_udata->inviter = inviter_handle;

  DEBUG ("received obsolete MUC invite from handle %u (%s), discoing room %s",
      inviter_handle, from, room);

  request = gabble_disco_request (priv->conn->disco, GABBLE_DISCO_TYPE_INFO,
      room, NULL, obsolete_invite_disco_cb, disco_udata, G_OBJECT (fac), NULL);

  if (request != NULL)
    {
      g_hash_table_insert (priv->disco_requests, request, NULL);
    }
  else
    {
      DEBUG ("obsolete MUC invite disco failed, freeing info");

      tp_handle_unref (contact_repo, inviter_handle);
      g_free (disco_udata->reason);
      g_slice_free (struct DiscoInviteData, disco_udata);
    }

  return TRUE;
}

/**
 * muc_factory_message_cb:
 *
 * Called by loudmouth when we get an incoming <message>.
 * We filter only groupchat and MUC messages, ignoring the rest.
 */
static LmHandlerResult
muc_factory_message_cb (LmMessageHandler *handler,
                        LmConnection *connection,
                        LmMessage *message,
                        gpointer user_data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (user_data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_ROOM);

  const gchar *from, *body, *subject, *id;
  time_t stamp;
  TpChannelTextMessageType msgtype;
  TpHandleRepoIface *handle_source;
  TpHandleType handle_type;
  TpHandle room_handle, handle;
  GabbleMucChannel *chan;
  gint state;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  gchar *room;
  LmMessageNode *subj_node;

  if (!gabble_message_util_parse_incoming_message (message, &from, &stamp,
        &msgtype, &id, &body, &state, &send_error, &delivery_status))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (conn_olpc_process_activity_properties_message (priv->conn, message,
        from))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (conn_olpc_process_activity_uninvite_message (priv->conn, message,
        from))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (process_muc_invite (fac, message, from, send_error))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (process_obsolete_invite (fac, message, from, body, send_error))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  /* check if a room with the jid exists */
  room = gabble_remove_resource (from);
  room_handle = tp_handle_lookup (room_repo, room, NULL, NULL);
  g_free (room);

  /* the message is nothing to do with MUC, do nothing */
  if (room_handle == 0)
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  /* find the MUC channel */
  chan = g_hash_table_lookup (priv->text_channels,
      GUINT_TO_POINTER (room_handle));

  if (chan == NULL)
    {
      NODE_DEBUG (message->node, "ignoring MUC message from known "
          "handle with no corresponding channel");

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  /* get the handle of the sender, which is either the room
   * itself or one of its members */
  if (strchr (from, '/') == NULL)
    {
      handle_source = room_repo;
      handle_type = TP_HANDLE_TYPE_ROOM;
      handle = room_handle;
      tp_handle_ref (room_repo, handle);
    }
  else
    {
      handle_source = contact_repo;
      handle_type = TP_HANDLE_TYPE_CONTACT;
      handle = tp_handle_ensure (contact_repo, from,
          GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);

      if (handle == 0)
        {
          NODE_DEBUG (message->node, "MUC message from invalid JID; ignoring");
          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }

      /* anything other than a type="groupchat" is from the person directly and
       * simply relayed by the MUC, so should be left to the normal handlers */
      if (lm_message_get_sub_type (message) != LM_MESSAGE_SUB_TYPE_GROUPCHAT)
        {
          tp_handle_unref (contact_repo, handle);

          return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
        }
    }

  if (body != NULL)
    _gabble_muc_channel_receive (chan, msgtype, handle_type, handle, stamp,
        id, body, message, send_error, delivery_status);

  if (send_error == GABBLE_TEXT_CHANNEL_SEND_NO_ERROR)
    {
      if (state != -1 && handle_type == TP_HANDLE_TYPE_CONTACT)
        _gabble_muc_channel_state_receive (chan, state, handle);

      subj_node = lm_message_node_get_child (message->node, "subject");
      if (subj_node != NULL)
        {
          subject = lm_message_node_get_value (subj_node);
          _gabble_muc_channel_handle_subject (chan, msgtype, handle_type, handle,
              stamp, subject, message);
        }
    }

  tp_handle_unref (handle_source, handle);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}


/**
 * connection_presence_muc_cb:
 * @handler: #LmMessageHandler for this message
 * @connection: #LmConnection that originated the message
 * @message: the presence message
 * @user_data: callback data
 *
 * Called by loudmouth when we get an incoming <presence>.
 */
static LmHandlerResult
muc_factory_presence_cb (LmMessageHandler *handler,
                            LmConnection *lmconn,
                            LmMessage *msg,
                            gpointer user_data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (user_data);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);
  const char *from;
  LmMessageSubType sub_type;
  GabbleMucChannel *muc_chan = NULL;
  LmMessageNode *x_node;
  TpHandle room_handle;

  g_assert (lmconn == priv->conn->lmconn);

  from = lm_message_node_get_attribute (msg->node, "from");

  if (from == NULL)
    {
      NODE_DEBUG (msg->node,
          "presence stanza without from attribute, ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  sub_type = lm_message_get_sub_type (msg);

  room_handle = gabble_get_room_handle_from_jid (room_repo, from);
  if (room_handle != 0)
    muc_chan = g_hash_table_lookup (priv->text_channels,
        GUINT_TO_POINTER (room_handle));

  /* is it an error and for a MUC? */
  if (sub_type == LM_MESSAGE_SUB_TYPE_ERROR
      && muc_chan != NULL)
    {
      _gabble_muc_channel_presence_error (muc_chan, from, msg->node);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  x_node = lm_message_node_get_child_with_namespace (msg->node, "x",
      NS_MUC_USER);

  /* is it a MUC member presence? */
  if (x_node != NULL)
    {
      if (muc_chan != NULL)
        {
          TpHandle handle;
          LmMessageNode *item_node;
          const gchar *owner_jid;

          handle = tp_handle_ensure (contact_repo, from,
              GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);
          if (handle == 0)
            {
              NODE_DEBUG (msg->node,
                  "discarding MUC presence from malformed jid");
              return LM_HANDLER_RESULT_REMOVE_MESSAGE;
            }

          item_node = lm_message_node_get_child (x_node, "item");
          if (item_node == NULL)
            {
              DEBUG ("node missing 'item' child, ignoring");
              return LM_HANDLER_RESULT_REMOVE_MESSAGE;
            }

          owner_jid = lm_message_node_get_attribute (item_node, "jid");
          /* We drop OLPC Gadget's inspector presence as activities
           * doesn't have to see it as a member of the room and the
           * presence cache should ignore it as well. */
          if (owner_jid != NULL &&
              !tp_strdiff (owner_jid, priv->conn->olpc_gadget_activity))
            {
              return LM_HANDLER_RESULT_REMOVE_MESSAGE;
            }

          _gabble_muc_channel_member_presence_updated (muc_chan, handle,
              msg, x_node, item_node);
          tp_handle_unref (contact_repo, handle);
        }
      else
        {
          NODE_DEBUG (msg->node, "discarding unexpected MUC member presence");

          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }
    }

  /* is it presence from an in-MUC JID containing tubes info? */

  if (muc_chan != NULL)
    {
      TpHandle handle;
      GabbleTubesChannel *tubes_chan;

      tubes_chan = g_hash_table_lookup (priv->tubes_channels,
          GUINT_TO_POINTER (room_handle));
      if (tubes_chan == NULL)
        {
          LmMessageNode *tubes_node;

          tubes_node = lm_message_node_get_child_with_namespace (
              msg->node, "tubes", NS_TUBES);

          if (tubes_node == NULL)
            /* presence doesn't contain tubes information, no need
             * to create a tubes channel */
            return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

          /* MUC Tubes channels (as opposed to the individual tubes) don't
           * have a well-defined initiator (they're a consensus) so use 0 */
          tubes_chan = new_tubes_channel (fac, room_handle, muc_chan,
              0, FALSE);
          tp_channel_manager_emit_new_channel (fac,
              TP_EXPORTABLE_CHANNEL (tubes_chan), NULL);
        }

      handle = tp_handle_ensure (contact_repo, from,
          GUINT_TO_POINTER (GABBLE_JID_ROOM_MEMBER), NULL);
      if (handle == 0)
        {
          NODE_DEBUG (msg->node,
              "discarding Tubes presence from malformed jid");
          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }

      gabble_tubes_channel_presence_updated (tubes_chan, handle,
          msg);
      tp_handle_unref (contact_repo, handle);
    }

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}


void
gabble_muc_factory_broadcast_presence (GabbleMucFactory *self)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (self);
  GHashTableIter iter;
  gpointer channel = NULL;

  g_hash_table_iter_init (&iter, priv->text_channels);

  while (g_hash_table_iter_next (&iter, NULL, &channel))
    {
      g_assert (GABBLE_IS_MUC_CHANNEL (channel));
      gabble_muc_channel_send_presence (GABBLE_MUC_CHANNEL (channel), NULL);
    }
}


static void
gabble_muc_factory_associate_request (GabbleMucFactory *self,
                                      gpointer channel,
                                      gpointer request)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (self);
  GSList *list = g_hash_table_lookup (priv->queued_requests, channel);

  g_assert (TP_IS_EXPORTABLE_CHANNEL (channel));

  g_hash_table_steal (priv->queued_requests, channel);
  list = g_slist_prepend (list, request);
  g_hash_table_insert (priv->queued_requests, channel, list);
}


static gboolean
cancel_queued_requests (gpointer k,
                        gpointer v,
                        gpointer d)
{
  GabbleMucFactory *self = GABBLE_MUC_FACTORY (d);
  GSList *requests_satisfied = v;
  GSList *iter;

  requests_satisfied = g_slist_reverse (requests_satisfied);

  for (iter = requests_satisfied; iter != NULL; iter = iter->next)
    {
      tp_channel_manager_emit_request_failed (self,
          iter->data, TP_ERRORS, TP_ERROR_DISCONNECTED,
          "Unable to complete this channel request, we're disconnecting!");
    }

  g_slist_free (requests_satisfied);

  return TRUE;
}


static void
gabble_muc_factory_close_all (GabbleMucFactory *self)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (self);

  DEBUG ("closing channels");

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->conn,
          priv->status_changed_id);
      priv->status_changed_id = 0;
    }

  if (priv->queued_requests != NULL)
    {
      g_hash_table_foreach_steal (priv->queued_requests,
          cancel_queued_requests, self);
      g_hash_table_destroy (priv->queued_requests);
      priv->queued_requests = NULL;
    }

  if (priv->text_needed_for_tubes != NULL)
    {
      g_hash_table_destroy (priv->text_needed_for_tubes);
      priv->text_needed_for_tubes = NULL;
    }

  if (priv->tubes_needed_for_tube != NULL)
    {
      g_hash_table_destroy (priv->tubes_needed_for_tube);
      priv->tubes_needed_for_tube = NULL;
    }

  /* Use a temporary variable because we don't want
   * muc_channel_closed_cb or tubes_channel_closed_cb to remove the channel
   * from the hash table a second time */
  if (priv->text_channels != NULL)
    {
      GHashTable *tmp = priv->text_channels;
      priv->text_channels = NULL;
      g_hash_table_destroy (tmp);
    }

  if (priv->tubes_channels != NULL)
    {
      GHashTable *tmp = priv->tubes_channels;
      priv->tubes_channels = NULL;
      g_hash_table_destroy (tmp);
    }

  if (priv->message_cb != NULL)
    {
      DEBUG ("removing callbacks");
      g_assert (priv->presence_cb != NULL);

      lm_connection_unregister_message_handler (priv->conn->lmconn,
          priv->message_cb, LM_MESSAGE_TYPE_MESSAGE);
      lm_message_handler_unref (priv->message_cb);
      priv->message_cb = NULL;

      lm_connection_unregister_message_handler (priv->conn->lmconn,
          priv->presence_cb, LM_MESSAGE_TYPE_PRESENCE);
      lm_message_handler_unref (priv->presence_cb);
      priv->presence_cb = NULL;
    }
}


static void
connection_status_changed_cb (GabbleConnection *conn,
                              guint status,
                              guint reason,
                              GabbleMucFactory *self)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (self);

  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
      DEBUG ("adding callbacks");
      g_assert (priv->message_cb == NULL);
      g_assert (priv->presence_cb == NULL);

      priv->message_cb = lm_message_handler_new (muc_factory_message_cb,
          self, NULL);
      lm_connection_register_message_handler (priv->conn->lmconn,
          priv->message_cb, LM_MESSAGE_TYPE_MESSAGE,
          LM_HANDLER_PRIORITY_NORMAL);

      priv->presence_cb = lm_message_handler_new (
          muc_factory_presence_cb, self, NULL);
      lm_connection_register_message_handler (priv->conn->lmconn,
          priv->presence_cb, LM_MESSAGE_TYPE_PRESENCE,
          LM_HANDLER_PRIORITY_NORMAL);

      break;

    case TP_CONNECTION_STATUS_DISCONNECTED:
      gabble_muc_factory_close_all (self);
      break;
    }
}


static GObject *
gabble_muc_factory_constructor (GType type, guint n_props,
                                GObjectConstructParam *props)
{
  GObject *obj = G_OBJECT_CLASS (gabble_muc_factory_parent_class)->
           constructor (type, n_props, props);
  GabbleMucFactory *self = GABBLE_MUC_FACTORY (obj);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (self);

  priv->status_changed_id = g_signal_connect (priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, obj);

  return obj;
}


struct _ForeachData
{
  TpExportableChannelFunc foreach;
  gpointer user_data;
};

static void
_foreach_slave (gpointer key, gpointer value, gpointer user_data)
{
  struct _ForeachData *data = (struct _ForeachData *) user_data;
  TpExportableChannel *channel = TP_EXPORTABLE_CHANNEL (value);

  data->foreach (channel, data->user_data);
}

static void
gabble_muc_factory_foreach_channel (TpChannelManager *manager,
                                    TpExportableChannelFunc foreach,
                                    gpointer user_data)
{
  GabbleMucFactory *fac = GABBLE_MUC_FACTORY (manager);
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (fac);
  struct _ForeachData data;
  GHashTableIter iter;
  gpointer value;

  data.user_data = user_data;
  data.foreach = foreach;

  g_hash_table_foreach (priv->text_channels, _foreach_slave, &data);

  g_hash_table_iter_init (&iter, priv->tubes_channels);
  while (g_hash_table_iter_next (&iter, NULL, &value))
  {
    TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (value);

    /* Add channels of type Channel.Type.Tubes */
    foreach (chan, user_data);

    /* Add channels of type Channel.Type.{Stream|DBus}Tube which live in the
     * GabbleTubesChannel object */
    gabble_tubes_channel_foreach (GABBLE_TUBES_CHANNEL (chan), foreach,
        user_data);
  }
}


/**
 * ensure_muc_channel:
 *
 * Create a MUC channel in response to RequestChannel.
 *
 * Return TRUE if it already existed, or return FALSE
 * if it needed to be created (so isn't ready yet).
 */
static gboolean
ensure_muc_channel (GabbleMucFactory *fac,
                    GabbleMucFactoryPrivate *priv,
                    TpHandle handle,
                    GabbleMucChannel **ret,
                    gboolean requested)
{
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;

  *ret = g_hash_table_lookup (priv->text_channels, GUINT_TO_POINTER (handle));

  if (*ret == NULL)
    {
      *ret = new_muc_channel (fac, handle, FALSE, base_conn->self_handle, NULL,
          requested);
      return FALSE;
    }

  if (_gabble_muc_channel_is_ready (*ret))
    return TRUE;
  else
    return FALSE;
}


void
gabble_muc_factory_handle_si_stream_request (GabbleMucFactory *self,
                                             GabbleBytestreamIface *bytestream,
                                             TpHandle room_handle,
                                             const gchar *stream_id,
                                             LmMessage *msg)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (self);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
     (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_ROOM);
  GabbleTubesChannel *chan;

  g_return_if_fail (tp_handle_is_valid (room_repo, room_handle, NULL));

  chan = g_hash_table_lookup (priv->tubes_channels,
      GUINT_TO_POINTER (room_handle));
  if (chan == NULL)
    {
      GError e = { GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "No tubes channel available for this MUC" };

      DEBUG ("tubes channel doesn't exist for muc %d", room_handle);
      gabble_bytestream_iface_close (bytestream, &e);
      return;
    }

  gabble_tubes_channel_bytestream_offered (chan, bytestream, msg);
}

GabbleMucChannel *
gabble_muc_factory_find_text_channel (GabbleMucFactory *self,
                                      TpHandle handle)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (self);

  return g_hash_table_lookup (priv->text_channels, GUINT_TO_POINTER (handle));
}


static const gchar * const muc_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const * muc_tubes_channel_fixed_properties =
    muc_channel_fixed_properties;

static const gchar * const muc_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    NULL
};

static const gchar * const * muc_tubes_channel_allowed_properties =
    muc_channel_allowed_properties;

static void
gabble_muc_factory_foreach_channel_class (TpChannelManager *manager,
    TpChannelManagerChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  GValue *channel_type_value, *handle_type_value;

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  /* no string value yet - we'll change it for each channel class */
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (handle_type_value, TP_HANDLE_TYPE_ROOM);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType",
      handle_type_value);

  /* Channel.Type.Text */
  g_value_set_static_string (channel_type_value, TP_IFACE_CHANNEL_TYPE_TEXT);
  func (manager, table, muc_channel_allowed_properties,
      user_data);

  /* Channel.Type.Tubes */
  g_value_set_static_string (channel_type_value, TP_IFACE_CHANNEL_TYPE_TUBES);
  func (manager, table, muc_tubes_channel_allowed_properties,
      user_data);

  /* Muc Channel.Type.StreamTube */
  g_value_set_static_string (channel_type_value,
      TP_IFACE_CHANNEL_TYPE_STREAM_TUBE);
  func (manager, table, gabble_tube_stream_channel_get_allowed_properties (),
      user_data);

  /* Muc Channel.Type.DBusTube */
  g_value_set_static_string (channel_type_value,
      TP_IFACE_CHANNEL_TYPE_DBUS_TUBE);
  func (manager, table, gabble_tube_dbus_channel_get_allowed_properties (),
      user_data);

  g_hash_table_destroy (table);
}

/* return TRUE if the text_channel associated is ready */
static gboolean
ensure_tubes_channel (GabbleMucFactory *self,
                      TpHandle handle,
                      GabbleTubesChannel **tubes_chan,
                      gboolean requested)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (self);
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;
  GabbleMucChannel *text_chan;
  gboolean result;

  result = ensure_muc_channel (self, priv, handle, &text_chan, FALSE);

  *tubes_chan = new_tubes_channel (self, handle, text_chan,
      base_conn->self_handle, requested);

  if (!result)
    {
      g_hash_table_insert (priv->text_needed_for_tubes, text_chan, *tubes_chan);
    }

  return result;
}

static gboolean
handle_text_channel_request (GabbleMucFactory *self,
                            gpointer request_token,
                            GHashTable *request_properties,
                            gboolean require_new,
                            TpHandle handle,
                            GError **error)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (self);
  GabbleMucChannel *text_chan;

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          muc_channel_fixed_properties, muc_channel_allowed_properties,
          error))
    return FALSE;

  if (ensure_muc_channel (self, priv, handle, &text_chan, TRUE))
    {
      if (require_new)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "That channel has already been created (or requested)");
          return FALSE;
        }
      else
        {
          tp_channel_manager_emit_request_already_satisfied (self,
              request_token, TP_EXPORTABLE_CHANNEL (text_chan));
        }
    }
  else
    {
      gabble_muc_factory_associate_request (self, text_chan,
          request_token);
    }

  return TRUE;

}

static gboolean
handle_tubes_channel_request (GabbleMucFactory *self,
                              gpointer request_token,
                              GHashTable *request_properties,
                              gboolean require_new,
                              TpHandle handle,
                              GError **error)
{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (self);
  GabbleTubesChannel *tubes_chan;

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          muc_tubes_channel_fixed_properties,
          muc_tubes_channel_allowed_properties,
          error))
    return FALSE;

  tubes_chan = g_hash_table_lookup (priv->tubes_channels,
      GUINT_TO_POINTER (handle));

  if (tubes_chan != NULL)
    {
      if (require_new)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "That channel has already been created (or requested)");
          return FALSE;
        }
      else
        {
          tp_channel_manager_emit_request_already_satisfied (self,
              request_token, TP_EXPORTABLE_CHANNEL (tubes_chan));
        }
    }
  else if (ensure_tubes_channel (self, handle, &tubes_chan, TRUE))
    {
      GSList *list = NULL;

      list = g_slist_prepend (list, request_token);
      tp_channel_manager_emit_new_channel (self,
          TP_EXPORTABLE_CHANNEL (tubes_chan), list);
      g_slist_free (list);
    }
  else
    {
      gabble_muc_factory_associate_request (self, tubes_chan,
          request_token);
    }

  return TRUE;
}

static gboolean
handle_tube_channel_request (GabbleMucFactory *self,
                             gpointer request_token,
                             GHashTable *request_properties,
                             gboolean require_new,
                             TpHandle handle,
                             GError **error)

{
  GabbleMucFactoryPrivate *priv = GABBLE_MUC_FACTORY_GET_PRIVATE (self);
  gboolean can_announce_now = TRUE;
  gboolean tubes_channel_created = FALSE;
  GabbleTubesChannel *tubes_chan;
  GabbleTubeIface *new_channel;

  tubes_chan = g_hash_table_lookup (priv->tubes_channels,
      GUINT_TO_POINTER (handle));
  if (tubes_chan == NULL)
    {
      /* Need to create a tubes channel */
      if (!ensure_tubes_channel (self, handle, &tubes_chan, FALSE))
      {
        /* We have to wait the tubes channel before announcing */
        can_announce_now = FALSE;

        gabble_muc_factory_associate_request (self, tubes_chan,
            request_token);
      }

      tubes_channel_created = TRUE;
    }

  g_assert (tubes_chan != NULL);

  new_channel = gabble_tubes_channel_tube_request (tubes_chan,
      request_token, request_properties, TRUE);
  g_assert (new_channel != NULL);

  if (can_announce_now)
    {
      GHashTable *channels;
      GSList *request_tokens;

      channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
        NULL, NULL);

      if (tubes_channel_created)
        g_hash_table_insert (channels, tubes_chan, NULL);

      request_tokens = g_slist_prepend (NULL, request_token);

      g_hash_table_insert (channels, new_channel, request_tokens);
      tp_channel_manager_emit_new_channels (self, channels);

      g_hash_table_destroy (channels);
      g_slist_free (request_tokens);
    }
  else
    {
      GSList *l;

      l = g_hash_table_lookup (priv->tubes_needed_for_tube, tubes_chan);
      g_hash_table_steal (priv->tubes_needed_for_tube, tubes_chan);

      l = g_slist_prepend (l, new_channel);
      g_hash_table_insert (priv->tubes_needed_for_tube, tubes_chan, l);
    }

  return TRUE;
}

static gboolean
handle_stream_tube_channel_request (GabbleMucFactory *self,
                                    gpointer request_token,
                                    GHashTable *request_properties,
                                    gboolean require_new,
                                    TpHandle handle,
                                    GError **error)
{
  const gchar *service;

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          muc_tubes_channel_fixed_properties,
          gabble_tube_stream_channel_get_allowed_properties (),
          error))
    return FALSE;

  /* "Service" is a mandatory, not-fixed property */
  service = tp_asv_get_string (request_properties,
            TP_IFACE_CHANNEL_TYPE_STREAM_TUBE ".Service");
  if (service == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Request does not contain the mandatory property '%s'",
          TP_IFACE_CHANNEL_TYPE_STREAM_TUBE ".Service");
      return FALSE;
    }

  return handle_tube_channel_request (self, request_token, request_properties,
      require_new, handle, error);
}

static gboolean
handle_dbus_tube_channel_request (GabbleMucFactory *self,
                                  gpointer request_token,
                                  GHashTable *request_properties,
                                  gboolean require_new,
                                  TpHandle handle,
                                  GError **error)
{
  const gchar *service;

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          muc_tubes_channel_fixed_properties,
          gabble_tube_dbus_channel_get_allowed_properties (),
          error))
    return FALSE;

  /* "ServiceName" is a mandatory, not-fixed property */
  service = tp_asv_get_string (request_properties,
      TP_IFACE_CHANNEL_TYPE_DBUS_TUBE ".ServiceName");
  if (service == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Request does not contain the mandatory property '%s'",
          TP_IFACE_CHANNEL_TYPE_DBUS_TUBE ".ServiceName");
      return FALSE;
    }

  return handle_tube_channel_request (self, request_token, request_properties,
      require_new, handle, error);
}

static gboolean
gabble_muc_factory_request (GabbleMucFactory *self,
                            gpointer request_token,
                            GHashTable *request_properties,
                            gboolean require_new)
{
  GError *error = NULL;
  TpHandle handle;
  const gchar *channel_type;

  if (tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_ROOM)
    return FALSE;

  channel_type = tp_asv_get_string (request_properties,
      TP_IFACE_CHANNEL ".ChannelType");

   if (tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TEXT) &&
       tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TUBES) &&
       tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE) &&
       tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE))
     return FALSE;

  /* validity already checked by TpBaseConnection */
  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);
  g_assert (handle != 0);

  if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TEXT))
    {
      if (handle_text_channel_request (self, request_token,
          request_properties, require_new, handle, &error))
        return TRUE;
    }
  else if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TUBES))
    {
      if (handle_tubes_channel_request (self, request_token,
          request_properties, require_new, handle, &error))
        return TRUE;
    }
  else if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
    {
      if (handle_stream_tube_channel_request (self, request_token,
          request_properties, require_new, handle, &error))
        return TRUE;
    }
  else if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE))
    {
      if (handle_dbus_tube_channel_request (self, request_token,
          request_properties, require_new, handle, &error))
        return TRUE;
    }
  else
    {
      g_assert_not_reached ();
    }

  /* Something failed */
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}


static gboolean
gabble_muc_factory_create_channel (TpChannelManager *manager,
                                   gpointer request_token,
                                   GHashTable *request_properties)
{
  GabbleMucFactory *self = GABBLE_MUC_FACTORY (manager);

  return gabble_muc_factory_request (self, request_token, request_properties,
      TRUE);
}


static gboolean
gabble_muc_factory_request_channel (TpChannelManager *manager,
                                    gpointer request_token,
                                    GHashTable *request_properties)
{
  GabbleMucFactory *self = GABBLE_MUC_FACTORY (manager);

  return gabble_muc_factory_request (self, request_token, request_properties,
      FALSE);
}


static gboolean
gabble_muc_factory_ensure_channel (TpChannelManager *manager,
                                    gpointer request_token,
                                    GHashTable *request_properties)
{
  GabbleMucFactory *self = GABBLE_MUC_FACTORY (manager);

  return gabble_muc_factory_request (self, request_token, request_properties,
      FALSE);
}


static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = gabble_muc_factory_foreach_channel;
  iface->foreach_channel_class = gabble_muc_factory_foreach_channel_class;
  iface->request_channel = gabble_muc_factory_request_channel;
  iface->create_channel = gabble_muc_factory_create_channel;
  iface->ensure_channel = gabble_muc_factory_ensure_channel;
}
