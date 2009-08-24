/*
 * search-manager.c - TpChannelManager implementation for ContactSearch channels
 * Copyright (C) 2009 Collabora Ltd.
 * Copyright (C) 2009 Nokia Corporation
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
#include "search-manager.h"

#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_SEARCH

#include "extensions/extensions.h"

#include "caps-channel-manager.h"
#include "connection.h"
#include "debug.h"
#include "disco.h"
#include "search-channel.h"
#include "util.h"

static void channel_manager_iface_init (gpointer, gpointer);
static void caps_channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleSearchManager, gabble_search_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER,
      caps_channel_manager_iface_init));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

struct _GabbleSearchManagerPrivate
{
  GabbleConnection *conn;

  /* Used to represent a set of channels.
   * Keys are GabbleSearchChannel *, values are an arbitrary non-NULL pointer.
   */
  GHashTable *channels;

  gchar *default_jud;

  gboolean dispose_has_run;
};

static void
gabble_search_manager_init (GabbleSearchManager *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GABBLE_TYPE_SEARCH_MANAGER,
      GabbleSearchManagerPrivate);

  self->priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      g_object_unref, NULL);

  self->priv->conn = NULL;
  self->priv->dispose_has_run = FALSE;
}

static void
gabble_search_manager_close_all (GabbleSearchManager *self)
{
  GList *chans, *l;

  if (self->priv->channels == NULL)
    return;

  DEBUG ("closing channels");

  /* We can't use a GHashTableIter as closing the channel while remove it from
   * the hash table and we can't modify a hash table while iterating on it. */
  chans = g_hash_table_get_keys (self->priv->channels);
  for (l = chans; l != NULL; l = g_list_next (l))
    {
      GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (l->data);

      gabble_search_channel_close (chan);
    }

  g_hash_table_destroy (self->priv->channels);
  self->priv->channels = NULL;
  g_list_free (chans);
}

static void
disco_item_found_cb (GabbleDisco *disco,
    GabbleDiscoItem *item,
    GabbleSearchManager *self)
{
  if (tp_strdiff (item->category, "directory") ||
      tp_strdiff (item->type, "user"))
    return;

  DEBUG ("Found contact directory: %s\n", item->jid);
  g_free (self->priv->default_jud);
  self->priv->default_jud = g_strdup (item->jid);
}

static void
connection_status_changed_cb (GabbleConnection *conn,
                              guint status,
                              guint reason,
                              GabbleSearchManager *self)
{
  switch (status)
    {
      case TP_CONNECTION_STATUS_CONNECTING:
        /* Track Search server available on the connection.
         *
         * The GabbleDisco object is created after the channel manager so we
         * can connect this signal in our constructor. */
        gabble_signal_connect_weak (self->priv->conn->disco, "item-found",
            G_CALLBACK (disco_item_found_cb), G_OBJECT (self));
        break;

      case TP_CONNECTION_STATUS_DISCONNECTED:
        gabble_search_manager_close_all (self);
        break;

      default:
        return;
    }
}

static GObject *
gabble_search_manager_constructor (GType type,
                                   guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj = G_OBJECT_CLASS (gabble_search_manager_parent_class)->
           constructor (type, n_props, props);
  GabbleSearchManager *self = GABBLE_SEARCH_MANAGER (obj);

  gabble_signal_connect_weak (self->priv->conn, "status-changed",
      G_CALLBACK (connection_status_changed_cb), G_OBJECT (obj));

  return obj;
}

static void
gabble_search_manager_dispose (GObject *object)
{
  GabbleSearchManager *fac = GABBLE_SEARCH_MANAGER (object);
  GabbleSearchManagerPrivate *priv = fac->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  gabble_search_manager_close_all (fac);

  if (G_OBJECT_CLASS (gabble_search_manager_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_search_manager_parent_class)->dispose (object);
}

static void
gabble_search_manager_finalize (GObject *object)
{
  GabbleSearchManager *fac = GABBLE_SEARCH_MANAGER (object);
  GabbleSearchManagerPrivate *priv = fac->priv;

  g_free (priv->default_jud);

  if (G_OBJECT_CLASS (gabble_search_manager_parent_class)->finalize)
    G_OBJECT_CLASS (gabble_search_manager_parent_class)->finalize (object);
}

static void
gabble_search_manager_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  GabbleSearchManager *self = GABBLE_SEARCH_MANAGER (object);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, self->priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_search_manager_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  GabbleSearchManager *self = GABBLE_SEARCH_MANAGER (object);

  switch (property_id) {
    case PROP_CONNECTION:
      self->priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_search_manager_class_init (GabbleSearchManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass,
      sizeof (GabbleSearchManagerPrivate));

  object_class->constructor = gabble_search_manager_constructor;
  object_class->dispose = gabble_search_manager_dispose;
  object_class->finalize = gabble_search_manager_finalize;

  object_class->get_property = gabble_search_manager_get_property;
  object_class->set_property = gabble_search_manager_set_property;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this ContactSearch manager.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}

static void
gabble_search_manager_foreach_channel (TpChannelManager *manager,
                                   TpExportableChannelFunc func,
                                   gpointer user_data)
{
  GabbleSearchManager *self = GABBLE_SEARCH_MANAGER (manager);
  GHashTableIter iter;
  gpointer chan;

  g_hash_table_iter_init (&iter, self->priv->channels);
  while (g_hash_table_iter_next (&iter, &chan, NULL))
    {
      /* Don't list channels which are not ready as they have not been
       * announced in NewChannels yet.*/
      if (gabble_search_channel_is_ready (GABBLE_SEARCH_CHANNEL (chan)))
          func (chan, user_data);
    }
}

static const gchar * const search_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    NULL
};

static const gchar * const search_channel_allowed_properties[] = {
    GABBLE_IFACE_CHANNEL_TYPE_CONTACT_SEARCH ".Server",
    NULL
};


static void
gabble_search_manager_foreach_channel_class (TpChannelManager *manager,
    TpChannelManagerChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  GValue *value;

  value = tp_g_value_slice_new_string (
      GABBLE_IFACE_CHANNEL_TYPE_CONTACT_SEARCH);
  g_hash_table_insert (table, (gchar *) search_channel_fixed_properties[0],
      value);

  func (manager, table, search_channel_allowed_properties, user_data);

  g_hash_table_destroy (table);
}

static void
remove_search_channel (GabbleSearchManager *self,
                       GabbleSearchChannel *chan)
{
  if (self->priv->channels != NULL)
    g_hash_table_remove (self->priv->channels, chan);
}

static void
search_channel_closed_cb (GabbleSearchChannel *chan,
                          GabbleSearchManager *self)
{
  tp_channel_manager_emit_channel_closed_for_object (self,
      (TpExportableChannel *) chan);
  remove_search_channel (self, chan);
}

typedef struct {
    GabbleSearchManager *self;
    gpointer request_token;
    gchar *server;
} RequestContext;

static RequestContext *
request_context_new (GabbleSearchManager *self,
                     gpointer request_token,
                     const gchar *server)
{
  RequestContext *ctx = g_slice_new (RequestContext);

  ctx->self = g_object_ref (self);
  ctx->request_token = request_token;
  ctx->server = g_strdup (server);

  return ctx;
}

static void
request_context_free (RequestContext *ctx)
{
  g_object_unref (ctx->self);
  g_free (ctx->server);
  g_slice_free (RequestContext, ctx);
}

static void
search_channel_ready_or_not_cb (GabbleSearchChannel *chan,
                                GQuark domain,
                                gint code,
                                const gchar *message,
                                RequestContext *ctx)
{
  if (domain == 0)
    {
      GSList *request_tokens = g_slist_prepend (NULL, ctx->request_token);

      tp_channel_manager_emit_new_channel (ctx->self,
          (TpExportableChannel *) chan, request_tokens);

      g_slist_free (request_tokens);
    }
  else
    {
      if (domain == GABBLE_XMPP_ERROR)
        {
          domain = TP_ERRORS;
          /* - Maybe CreateChannel should be specced to raise PermissionDenied?
           *   Then we could map XMPP_ERROR_FORBIDDEN to that.
           * - Should XMPP_ERROR_JID_MALFORMED be mapped to InvalidArgument?
           */
          code = TP_ERROR_NOT_AVAILABLE;
          /* Do we want to prefix the error string with something? */
        }
      else
        {
          g_assert (domain == TP_ERRORS);
        }

      tp_channel_manager_emit_request_failed (ctx->self,
          ctx->request_token, domain, code, message);
      remove_search_channel (ctx->self, chan);
    }

  request_context_free (ctx);
}

static void
new_search_channel (GabbleSearchManager *self,
                    const gchar *server,
                    gpointer request_token)
{
  GabbleSearchManagerPrivate *priv = self->priv;
  GabbleSearchChannel *chan;

  g_assert (server != NULL);

  chan = g_object_new (GABBLE_TYPE_SEARCH_CHANNEL,
      "connection", priv->conn,
      "server", server,
      NULL);
  g_hash_table_insert (priv->channels, chan, priv->channels);
  g_signal_connect (chan, "closed", (GCallback) search_channel_closed_cb, self);

  g_signal_connect (chan, "ready-or-not",
      (GCallback) search_channel_ready_or_not_cb,
      request_context_new (self, request_token, server));
}

static gboolean
gabble_search_manager_create_channel (TpChannelManager *manager,
                                      gpointer request_token,
                                      GHashTable *request_properties)
{
  GabbleSearchManager *self = GABBLE_SEARCH_MANAGER (manager);
  GError *error = NULL;
  const gchar *channel_type;
  const gchar *server;

  channel_type = tp_asv_get_string (request_properties,
      TP_IFACE_CHANNEL ".ChannelType");

  if (tp_strdiff (channel_type, GABBLE_IFACE_CHANNEL_TYPE_CONTACT_SEARCH))
    return FALSE;

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          search_channel_fixed_properties, search_channel_allowed_properties,
          &error))
    goto error;

  server = tp_asv_get_string (request_properties,
      GABBLE_IFACE_CHANNEL_TYPE_CONTACT_SEARCH ".Server");

  if (server == NULL)
    {
      if (self->priv->default_jud == NULL)
        {
          error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "No Server has been specified and no server has been discovered "
              "on the connection");
          goto error;
        }

      DEBUG ("No Server specified; use %s as default", self->priv->default_jud);
      server = self->priv->default_jud;
    }

  new_search_channel (self, server, request_token);
  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = gabble_search_manager_foreach_channel;
  iface->foreach_channel_class = gabble_search_manager_foreach_channel_class;

  iface->create_channel = gabble_search_manager_create_channel;
  iface->request_channel = gabble_search_manager_create_channel;

  /* Ensuring these channels doesn't really make much sense. */
  iface->ensure_channel = NULL;
}

static void
caps_channel_manager_iface_init (gpointer g_iface,
                                 gpointer iface_data)
{
  /* Leave everything unimplemented. */
}
