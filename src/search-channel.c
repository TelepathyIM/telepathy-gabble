/*
 * search-channel.c - implementation of ContactSearch channels
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
#include "search-channel.h"

#include <string.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-channel.h>

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_SEARCH
#include "base-channel.h"
#include "debug.h"
#include "namespaces.h"
#include "util.h"

#include "extensions/extensions.h"

static const gchar *gabble_search_channel_interfaces[] = {
    NULL
};

/* properties */
enum
{
  PROP_CHANNEL_PROPERTIES = 1,
  PROP_SEARCH_STATE,
  PROP_AVAILABLE_SEARCH_KEYS,
  PROP_SERVER,
  LAST_PROPERTY
};

/* signal enum */
enum
{
    PROBED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */

struct _GabbleSearchChannelPrivate
{
  GabbleChannelContactSearchState state;
  gchar **available_search_keys;
  gchar *server;
};

static void channel_iface_init (gpointer, gpointer);
static void contact_search_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleSearchChannel, gabble_search_channel,
    GABBLE_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CHANNEL_TYPE_CONTACT_SEARCH,
        contact_search_iface_init);
    )

static void
ensure_closed (GabbleSearchChannel *chan)
{
  if (chan->base.closed)
    {
      DEBUG ("Already closed, doing nothing");
    }
  else
    {
      DEBUG ("Emitting Closed");
      chan->base.closed = TRUE;
      tp_svc_channel_emit_closed (chan);
    }
}

static void
parse_search_field_response (GabbleSearchChannel *chan,
                             LmMessageNode *query_node)
{
  LmMessageNode *x_node;
  LmMessageNode *field;
  GPtrArray *search_keys;

  x_node = lm_message_node_get_child_with_namespace (query_node, "x",
      NS_X_DATA);

  if (x_node != NULL)
    {
      DEBUG ("response contains an XForm, which is not yet implemented");
      DEBUG ("failing the channel");

      g_signal_emit (chan, signals[PROBED], 0, FALSE);
      return;
    }

  search_keys = g_ptr_array_new ();

  for (field = query_node->children; field != NULL; field = field->next)
    {
      if (!strcmp (field->name, "instructions"))
        {
          DEBUG ("server gave us some instructions: %s",
              lm_message_node_get_value (field));
        }
#define MAP_FIELD_TO(xep_name, tp_name) \
      else if (!strcmp (field->name, xep_name)) \
        { \
          g_ptr_array_add (search_keys, tp_name); \
        }
      MAP_FIELD_TO ("first", "x-n-given")
      MAP_FIELD_TO ("last", "x-n-family")
      MAP_FIELD_TO ("nick", "nickname")
      MAP_FIELD_TO ("email", "email")
#undef MAP_FIELD_TO
      else
        {
          DEBUG ("%s is not a field defined in XEP 0055",
              field->name);
          DEBUG ("giving up on this channel");

          g_signal_emit (chan, signals[PROBED], 0, FALSE);
          g_ptr_array_free (search_keys, TRUE);
          return;
        }
    }

  DEBUG ("extracted available fields");
  g_ptr_array_add (search_keys, NULL);
  chan->priv->available_search_keys = (gchar **) g_ptr_array_free (search_keys, FALSE);
  chan->base.closed = FALSE;
  g_signal_emit (chan, signals[PROBED], 0, TRUE);
}

static LmHandlerResult
query_reply_cb (GabbleConnection *conn,
                LmMessage *sent_msg,
                LmMessage *reply_msg,
                GObject *object,
                gpointer user_data)
{
  GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (object);
  LmMessageNode *query_node;
  GError *err = NULL;

  query_node = lm_message_node_get_child_with_namespace (reply_msg->node,
      "query", NS_SEARCH);

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      err = gabble_message_get_xmpp_error (reply_msg);

      if (err == NULL)
        err = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
            "server gave us an error we don't understand");
    }
  else if (NULL == query_node)
    {
      err = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "server is chock-full of bees");
    }

  if (err != NULL)
    {
      DEBUG ("got an error back from %s: %s", chan->priv->server, err->message);

      g_signal_emit (chan, signals[PROBED], 0, FALSE);
    }
  else
    {
      parse_search_field_response (chan, query_node);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
request_search_fields (GabbleSearchChannel *chan)
{
  LmMessage *msg;
  LmMessageNode *lm_node;

  msg = lm_message_new_with_sub_type (chan->priv->server, LM_MESSAGE_TYPE_IQ,
      LM_MESSAGE_SUB_TYPE_GET);
  lm_node = lm_message_node_add_child (msg->node, "query", NULL);
  lm_message_node_set_attribute (lm_node, "xmlns", NS_SEARCH);

  if (! _gabble_connection_send_with_reply (chan->base.conn, msg,
            query_reply_cb, (GObject *) chan, NULL, NULL))
    {
      g_signal_emit (chan, signals[PROBED], 0, FALSE);
    }

  lm_message_unref (msg);
}

static void
gabble_search_channel_init (GabbleSearchChannel *self)
{
  GabbleSearchChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_SEARCH_CHANNEL, GabbleSearchChannelPrivate);

  self->priv = priv;

}

static GObject *
gabble_search_channel_constructor (GType type,
                                   guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GabbleSearchChannel *chan;
  GabbleBaseChannel *base;
  TpBaseConnection *conn;

  obj = G_OBJECT_CLASS (gabble_search_channel_parent_class)->constructor (
      type, n_props, props);
  chan = GABBLE_SEARCH_CHANNEL (obj);
  base = GABBLE_BASE_CHANNEL (obj);
  conn = (TpBaseConnection *) base->conn;

  base->channel_type = GABBLE_IFACE_CHANNEL_TYPE_CONTACT_SEARCH;
  base->interfaces = gabble_search_channel_interfaces;
  base->target_type = TP_HANDLE_TYPE_NONE;
  base->target = 0;
  base->initiator = conn->self_handle;

  base->object_path = g_strdup_printf ("%s/ContactSearchChannel%p",
      conn->object_path, obj);

  /* The channel only "opens" when it's found out that the server really does
   * speak XEP 0055 and knows which fields are supported.
   */
  base->closed = TRUE;

  request_search_fields (chan);

  return obj;
}

static void
gabble_search_channel_finalize (GObject *obj)
{
  GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (obj);

  ensure_closed (chan);

  g_free (chan->priv->server);
}

static void
gabble_search_channel_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (object);

  switch (property_id)
    {
      case PROP_SEARCH_STATE:
        g_value_set_uint (value, chan->priv->state);
        break;
      case PROP_AVAILABLE_SEARCH_KEYS:
        g_value_set_boxed (value, chan->priv->available_search_keys);
        break;
      case PROP_SERVER:
        g_value_set_string (value, chan->priv->server);
        break;
      case PROP_CHANNEL_PROPERTIES:
        g_value_take_boxed (value,
            tp_dbus_properties_mixin_make_properties_hash (object,
                TP_IFACE_CHANNEL, "TargetHandle",
                TP_IFACE_CHANNEL, "TargetHandleType",
                TP_IFACE_CHANNEL, "ChannelType",
                TP_IFACE_CHANNEL, "TargetID",
                TP_IFACE_CHANNEL, "InitiatorHandle",
                TP_IFACE_CHANNEL, "InitiatorID",
                TP_IFACE_CHANNEL, "Requested",
                TP_IFACE_CHANNEL, "Interfaces",
                GABBLE_IFACE_CHANNEL_TYPE_CONTACT_SEARCH, "AvailableSearchKeys",
                GABBLE_IFACE_CHANNEL_TYPE_CONTACT_SEARCH, "Server",
                NULL));
      break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_search_channel_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (object);

  switch (property_id)
    {
      case PROP_SEARCH_STATE:
        {
          GabbleChannelContactSearchState state = g_value_get_uint (value);

          /* The search state can only go forward because it can't find
           * reverse
           */
          g_return_if_fail (state > chan->priv->state);

          chan->priv->state = state;
          gabble_svc_channel_type_contact_search_emit_search_state_changed (
              chan, state);
          break;
        }
      case PROP_SERVER:
        chan->priv->server = g_value_dup_string (value);
        g_assert (chan->priv->server != NULL);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_search_channel_class_init (GabbleSearchChannelClass *klass)
{
  static TpDBusPropertiesMixinPropImpl search_channel_props[] = {
      { "SearchState", "search-state", NULL },
      { "AvailableSearchKeys", "available-search-keys", NULL },
      { "Server", "server", NULL },
      { NULL }
  };
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (GabbleSearchChannelPrivate));

  object_class->constructor = gabble_search_channel_constructor;
  object_class->finalize = gabble_search_channel_finalize;

  object_class->get_property = gabble_search_channel_get_property;
  object_class->set_property = gabble_search_channel_set_property;

  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");

  param_spec = g_param_spec_uint ("search-state", "Search state",
      "The current state of the search represented by this channel",
      GABBLE_CHANNEL_CONTACT_SEARCH_STATE_NOT_STARTED,
      GABBLE_CHANNEL_CONTACT_SEARCH_STATE_COMPLETED,
      GABBLE_CHANNEL_CONTACT_SEARCH_STATE_NOT_STARTED,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SEARCH_STATE,
      param_spec);

  param_spec = g_param_spec_boxed ("available-search-keys",
      "Available search keys",
      "The set of search keys supported by this channel",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_AVAILABLE_SEARCH_KEYS,
      param_spec);

  param_spec = g_param_spec_string ("server", "Search server",
      "The user directory server used by this search",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SERVER, param_spec);

  /* Emitted with argument TRUE if the service's supported search fields have
   * been discovered, and with argument FALSE if the service isn't actually a
   * XEP 0055 repository.
   */
  signals[PROBED] =
    g_signal_new ("probed",
                  G_OBJECT_CLASS_TYPE (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

  tp_dbus_properties_mixin_implement_interface (object_class,
      GABBLE_IFACE_QUARK_CHANNEL_TYPE_CONTACT_SEARCH,
      tp_dbus_properties_mixin_getter_gobject_properties, NULL,
      search_channel_props);
}

/**
 * gabble_search_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_search_channel_close (TpSvcChannel *iface,
                             DBusGMethodInvocation *context)
{
  GabbleSearchChannel *chan = GABBLE_SEARCH_CHANNEL (iface);

  ensure_closed (chan);

  tp_svc_channel_return_from_close (context);
}

static void
channel_iface_init (gpointer g_iface,
                    gpointer iface_data)
{
  TpSvcChannelClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, gabble_search_channel_##x)
  IMPLEMENT(close);
#undef IMPLEMENT
}

static void
gabble_search_channel_search (GabbleSvcChannelTypeContactSearch *self,
                              GHashTable *terms,
                              DBusGMethodInvocation *context)
{
  tp_dbus_g_method_return_not_implemented (context);
}

static void
contact_search_iface_init (gpointer g_iface,
                           gpointer iface_data)
{
  GabbleSvcChannelTypeContactSearchClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_channel_type_contact_search_implement_##x (\
    klass, gabble_search_channel_##x)
  IMPLEMENT(search);
#undef IMPLEMENT
}
