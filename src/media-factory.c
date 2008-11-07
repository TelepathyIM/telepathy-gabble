/*
 * media-factory.c - Source for GabbleMediaFactory
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
#include "media-factory.h"

#define DBUS_API_SUBJECT_TO_CHANGE

#include <stdlib.h>
#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "connection.h"
#include "debug.h"
#include "jingle-factory.h"
#include "jingle-media-rtp.h"
#include "jingle-session.h"
#include "media-channel.h"
#include "namespaces.h"
#include "text-mixin.h"
#include "util.h"

static void channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleMediaFactory, gabble_media_factory,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

typedef struct _GabbleMediaFactoryPrivate GabbleMediaFactoryPrivate;
struct _GabbleMediaFactoryPrivate
{
  GabbleConnection *conn;
  gulong status_changed_id;

  GPtrArray *channels;
  guint channel_index;

  GHashTable *session_chans;

  gboolean dispose_has_run;
};

#define GABBLE_MEDIA_FACTORY_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MEDIA_FACTORY, \
                                GabbleMediaFactoryPrivate))

static void
gabble_media_factory_init (GabbleMediaFactory *fac)
{
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

  priv->channels = g_ptr_array_sized_new (1);
  priv->channel_index = 0;

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;

  priv->session_chans = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, NULL);
}


static void gabble_media_factory_close_all (GabbleMediaFactory *fac);


static void
gabble_media_factory_dispose (GObject *object)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (object);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  gabble_media_factory_close_all (fac);
  g_assert (priv->channels == NULL);

  if (priv->session_chans)
    {
      g_assert (g_hash_table_size (priv->session_chans) == 0);
      g_hash_table_destroy (priv->session_chans);
      priv->session_chans = NULL;
    }

  if (G_OBJECT_CLASS (gabble_media_factory_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_media_factory_parent_class)->dispose (object);
}

static void
gabble_media_factory_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (object);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

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
gabble_media_factory_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (object);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static void gabble_media_factory_constructed (GObject *object);


static void
gabble_media_factory_class_init (GabbleMediaFactoryClass *gabble_media_factory_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_media_factory_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_media_factory_class,
      sizeof (GabbleMediaFactoryPrivate));

  object_class->constructed = gabble_media_factory_constructed;
  object_class->dispose = gabble_media_factory_dispose;

  object_class->get_property = gabble_media_factory_get_property;
  object_class->set_property = gabble_media_factory_set_property;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this media channel factory object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

}

static GabbleMediaChannel *new_media_channel (GabbleMediaFactory *fac,
    GabbleJingleSession *sess, TpHandle maybe_peer);
static void media_channel_closed_cb (GabbleMediaChannel *chan,
    gpointer user_data);

static gboolean
_remove_sid_mapping (gpointer key, gpointer value, gpointer user_data)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (value);
  GabbleMediaChannel *target_chan = GABBLE_MEDIA_CHANNEL (user_data);

  if (chan == target_chan) return TRUE;
  return FALSE;
}

/**
 * media_channel_closed_cb:
 *
 * Signal callback for when a media channel is closed. Removes the references
 * that #GabbleMediaFactory holds to them. Also removes all the sessions for
 * the closed channel.
 */
static void
media_channel_closed_cb (GabbleMediaChannel *chan, gpointer user_data)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (user_data);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

  tp_channel_manager_emit_channel_closed_for_object (fac,
      TP_EXPORTABLE_CHANNEL (chan));

  if (priv->channels != NULL)
    {
      DEBUG ("removing media channel %p with ref count %d",
          chan, G_OBJECT (chan)->ref_count);

      g_ptr_array_remove (priv->channels, chan);
      g_object_unref (chan);
    }

  if (priv->session_chans != NULL)
    {
      g_hash_table_foreach_remove (priv->session_chans, _remove_sid_mapping,
          chan);
    }
}

/**
 * new_media_channel
 *
 * Creates a new empty GabbleMediaChannel.
 */
static GabbleMediaChannel *
new_media_channel (GabbleMediaFactory *fac,
                   GabbleJingleSession *sess,
                   TpHandle maybe_peer)
{
  GabbleMediaFactoryPrivate *priv;
  TpBaseConnection *conn;
  GabbleMediaChannel *chan;
  gchar *object_path;

  g_assert (GABBLE_IS_MEDIA_FACTORY (fac));

  priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);
  conn = (TpBaseConnection *) priv->conn;

  object_path = g_strdup_printf ("%s/MediaChannel%u",
      conn->object_path, priv->channel_index);
  priv->channel_index += 1;

  chan = g_object_new (GABBLE_TYPE_MEDIA_CHANNEL,
                       "connection", priv->conn,
                       "factory", fac,
                       "object-path", object_path,
                       "session", sess,
                       "initial-peer", maybe_peer,
                       NULL);

  DEBUG ("object path %s", object_path);

  g_signal_connect (chan, "closed", (GCallback) media_channel_closed_cb, fac);

  g_ptr_array_add (priv->channels, chan);

  g_free (object_path);

  return chan;
}

static void
gabble_media_factory_close_all (GabbleMediaFactory *fac)
{
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

  DEBUG ("closing channels");

  if (priv->channels != NULL)
    {
      GPtrArray *tmp = priv->channels;
      guint i;

      priv->channels = NULL;

      for (i = 0; i < tmp->len; i++)
        {
          GabbleMediaChannel *chan = g_ptr_array_index (tmp, i);

          DEBUG ("about to unref channel with ref_count %d",
                   G_OBJECT (chan)->ref_count);

          g_object_unref (chan);
        }

      g_ptr_array_free (tmp, TRUE);
    }

  if (priv->session_chans != NULL)
    {
      g_hash_table_destroy (priv->session_chans);
      priv->session_chans = NULL;
    }

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->conn,
          priv->status_changed_id);
      priv->status_changed_id = 0;
    }
}

static void
new_jingle_session_cb (GabbleJingleFactory *jf, GabbleJingleSession *sess, gpointer data)
{
  GabbleMediaFactory *self = GABBLE_MEDIA_FACTORY (data);

  if (gabble_jingle_session_get_content_type (sess) ==
      GABBLE_TYPE_JINGLE_MEDIA_RTP)
    {
      GabbleMediaChannel *chan = new_media_channel (self, sess, sess->peer);
      tp_channel_manager_emit_new_channel (self,
          TP_EXPORTABLE_CHANNEL (chan), NULL);
    }
}

static void
connection_status_changed_cb (GabbleConnection *conn,
                              guint status,
                              guint reason,
                              GabbleMediaFactory *self)
{
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (self);

  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
      g_signal_connect (priv->conn->jingle_factory, "new-session",
          G_CALLBACK (new_jingle_session_cb), self);
      break;

    case TP_CONNECTION_STATUS_DISCONNECTED:
      gabble_media_factory_close_all (self);
      break;
    }
}


static void
gabble_media_factory_constructed (GObject *object)
{
  void (*chain_up) (GObject *) =
      G_OBJECT_CLASS (gabble_media_factory_parent_class)->constructed;
  GabbleMediaFactory *self = GABBLE_MEDIA_FACTORY (object);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (self);

  if (chain_up != NULL)
    chain_up (object);

  priv->status_changed_id = g_signal_connect (priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, object);
}


static void
gabble_media_factory_foreach_channel (TpChannelManager *manager,
                                      TpExportableChannelFunc foreach,
                                      gpointer user_data)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (manager);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);
  guint i;

  for (i = 0; i < priv->channels->len; i++)
    {
      TpExportableChannel *channel = TP_EXPORTABLE_CHANNEL (
          g_ptr_array_index (priv->channels, i));

      foreach (channel, user_data);
    }
}


static const gchar * const media_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const named_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    NULL
};

/* not advertised in foreach_channel_class - can only be requested with
 * RequestChannel, not with CreateChannel/EnsureChannel */
static const gchar * const anon_channel_allowed_properties[] = {
    NULL
};


static void
gabble_media_factory_foreach_channel_class (TpChannelManager *manager,
    TpChannelManagerChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  GValue *value;

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType", value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType", value);

  func (manager, table, named_channel_allowed_properties, user_data);

  g_hash_table_destroy (table);
}


typedef enum
{
  METHOD_REQUEST,
  METHOD_CREATE,
  METHOD_ENSURE,
} RequestMethod;


static gboolean
gabble_media_factory_requestotron (TpChannelManager *manager,
                                   gpointer request_token,
                                   GHashTable *request_properties,
                                   RequestMethod method)
{
  GabbleMediaFactory *self = GABBLE_MEDIA_FACTORY (manager);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (self);
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleType handle_type;
  TpHandle handle;
  GabbleMediaChannel *channel = NULL;
  GError *error = NULL;
  GSList *request_tokens;
  gboolean require_target_handle;
  gboolean add_peer_to_remote_pending;

  /* Supported modes of operation:
   * - RequestChannel(None, 0):
   *     channel is anonymous;
   *     caller uses RequestStreams to set the peer and start the call.
   * - RequestChannel(Contact, n) where n != 0:
   *     channel has TargetHandle=n;
   *     n is in remote pending;
   *     call is started when caller calls RequestStreams.
   * - CreateChannel({THT: Contact, TH: n}):
   *     channel has TargetHandle=n
   *     n is not in the group interface at all
   *     call is started when caller calls RequestStreams.
   * - EnsureChannel({THT: Contact, TH: n}):
   *     look for a channel whose peer is n, and return that if found with
   *       whatever properties and group membership it has;
   *     otherwise the same as into CreateChannel
   */
  switch (method)
    {
    case METHOD_REQUEST:
      require_target_handle = FALSE;
      add_peer_to_remote_pending = TRUE;
      break;
    case METHOD_CREATE:
    case METHOD_ENSURE:
      require_target_handle = TRUE;
      add_peer_to_remote_pending = FALSE;
      break;
    default:
      g_assert_not_reached ();
    }

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
        TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
    return FALSE;

  handle_type = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", NULL);

  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);

  switch (handle_type)
    {
    case TP_HANDLE_TYPE_NONE:
      /* already checked by TpBaseConnection */
      g_assert (handle == 0);

      if (require_target_handle)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
              "A valid Contact handle must be provided when requesting a media "
              "channel");
          goto error;
        }

      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              media_channel_fixed_properties, anon_channel_allowed_properties,
              &error))
        goto error;

      channel = new_media_channel (self, NULL, 0);
      break;

    case TP_HANDLE_TYPE_CONTACT:
      /* validity already checked by TpBaseConnection */
      g_assert (handle != 0);

      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              media_channel_fixed_properties, named_channel_allowed_properties,
              &error))
        goto error;

      if (method == METHOD_ENSURE)
        {
          guint i;
          TpHandle peer = 0;

          for (i = 0; i < priv->channels->len; i++)
            {
              channel = g_ptr_array_index (priv->channels, i);
              g_object_get (channel, "peer", &peer, NULL);

              if (peer == handle)
                {
                  tp_channel_manager_emit_request_already_satisfied (self,
                      request_token, TP_EXPORTABLE_CHANNEL (channel));
                  return TRUE;
                }
            }
        }

      channel = new_media_channel (self, NULL, handle);

      if (add_peer_to_remote_pending)
        {
          if (!_gabble_media_channel_add_member ((GObject *) channel, handle,
                "", &error))
            {
              gabble_media_channel_close (channel);
              goto error;
            }
        }

      break;

    default:
      return FALSE;
    }

  g_assert (channel != NULL);

  request_tokens = g_slist_prepend (NULL, request_token);
  tp_channel_manager_emit_new_channel (self,
      TP_EXPORTABLE_CHANNEL (channel), request_tokens);
  g_slist_free (request_tokens);

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}


static gboolean
gabble_media_factory_request_channel (TpChannelManager *manager,
                                      gpointer request_token,
                                      GHashTable *request_properties)
{
  return gabble_media_factory_requestotron (manager, request_token,
      request_properties, METHOD_REQUEST);
}


static gboolean
gabble_media_factory_create_channel (TpChannelManager *manager,
                                     gpointer request_token,
                                     GHashTable *request_properties)
{
  return gabble_media_factory_requestotron (manager, request_token,
      request_properties, METHOD_CREATE);
}


static gboolean
gabble_media_factory_ensure_channel (TpChannelManager *manager,
                                     gpointer request_token,
                                     GHashTable *request_properties)
{
  return gabble_media_factory_requestotron (manager, request_token,
      request_properties, METHOD_ENSURE);
}


static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = gabble_media_factory_foreach_channel;
  iface->foreach_channel_class = gabble_media_factory_foreach_channel_class;
  iface->request_channel = gabble_media_factory_request_channel;
  iface->create_channel = gabble_media_factory_create_channel;
  iface->ensure_channel = gabble_media_factory_ensure_channel;
}
