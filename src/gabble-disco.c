/*
 * gabble-disco.c - Source for Gabble service discovery
 *
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
 *
 * -- LETS DISCO!!!  \o/ \o_ _o/ /\o/\ _/o/- -\o\_ --
 */

#define DBUS_API_SUBJECT_TO_CHANGE

#include <string.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "telepathy-helpers.h"

#include "gabble-connection.h"
#include "gabble-disco.h"

#define DEFAULT_REQUEST_TIMEOUT 20000

/* Properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

G_DEFINE_TYPE(GabbleDisco, gabble_disco, G_TYPE_OBJECT);

typedef struct _GabbleDiscoPrivate GabbleDiscoPrivate;
struct _GabbleDiscoPrivate
{
  GabbleConnection *connection;
  GList *requests;
  gboolean dispose_has_run;
};

struct _GabbleDiscoRequest
{
  GabbleDisco *disco;
  guint timer_id;

  GabbleDiscoType type;
  gchar *jid;
  gchar *node;
  GabbleDiscoCb callback;
  gpointer user_data;
};

GQuark
gabble_disco_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("gabble-disco-error");
  return quark;
}

#define GABBLE_DISCO_GET_PRIVATE(o)     ((GabbleDiscoPrivate*)((o)->priv));

static void
gabble_disco_init (GabbleDisco *obj)
{
  GabbleDiscoPrivate *priv = 
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_DISCO, GabbleDiscoPrivate);
  obj->priv = priv;

}

static void gabble_disco_set_property (GObject *object, guint property_id, 
    const GValue *value, GParamSpec *pspec);
static void gabble_disco_get_property (GObject *object, guint property_id, 
    GValue *value, GParamSpec *pspec);
static void gabble_disco_dispose (GObject *object);
static void gabble_disco_finalize (GObject *object);

static void
gabble_disco_class_init (GabbleDiscoClass *gabble_disco_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_disco_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_disco_class, sizeof (GabbleDiscoPrivate));

  object_class->get_property = gabble_disco_get_property;
  object_class->set_property = gabble_disco_set_property;

  object_class->dispose = gabble_disco_dispose;
  object_class->finalize = gabble_disco_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "XMPP Discovery object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}

static void
gabble_disco_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GabbleDisco *chan = GABBLE_DISCO (object);
  GabbleDiscoPrivate *priv = GABBLE_DISCO_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->connection);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_disco_set_property (GObject     *object,
                           guint        property_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  GabbleDisco *chan = GABBLE_DISCO (object);
  GabbleDiscoPrivate *priv = GABBLE_DISCO_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void clear_timer_and_delete_request (GabbleDiscoRequest *request);

void
gabble_disco_dispose (GObject *object)
{
  GabbleDisco *self = GABBLE_DISCO (object);
  GabbleDiscoPrivate *priv = GABBLE_DISCO_GET_PRIVATE (self);
  GList *li;
  DBusGProxy *bus_proxy;
  bus_proxy = tp_get_bus_proxy ();

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_debug ("%s: dispose called", G_STRFUNC);

  for (li = g_list_first (priv->requests); li; li = li->next)
    {
      clear_timer_and_delete_request (li->data);
    }

  g_list_free (priv->requests);

  if (G_OBJECT_CLASS (gabble_disco_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_disco_parent_class)->dispose (object);
}

void
gabble_disco_finalize (GObject *object)
{
  g_debug ("%s called with %p", G_STRFUNC, object);

  G_OBJECT_CLASS (gabble_disco_parent_class)->finalize (object);
}


/**
 * gabble_disco_new:
 * @conn: The #GabbleConnection to use for service discovery
 *
 * Creates an object to use for Jabber service discovery (DISCO)
 * There should be one of these per connection
 */
GabbleDisco *
gabble_disco_new (GabbleConnection *conn)
{
  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);
  return GABBLE_DISCO (g_object_new (GABBLE_TYPE_DISCO, "connection", conn));
}


static void
delete_request (GabbleDiscoRequest *request)
{
  GabbleDiscoPrivate *priv = GABBLE_DISCO_GET_PRIVATE (request->disco);
  g_return_if_fail (request != NULL);

  priv->requests = g_list_remove (priv->requests, request);
  g_free (request->jid);
  g_free (request);
}

static gboolean
timeout_request (gpointer data)
{
  GabbleDiscoRequest *request = (GabbleDiscoRequest*) data;
  GError *err;
  g_return_val_if_fail (data != NULL, FALSE);

  err = g_error_new (GABBLE_DISCO_ERROR, GABBLE_DISCO_ERROR_TIMEOUT, 
      "Request for %s on %s timed out",
      (request->type == GABBLE_DISCO_TYPE_INFO)?"info":"items",
      request->jid);
  (request->callback)(request->disco, request->jid, request->node, NULL, err, request->user_data);
  g_error_free (err);

  delete_request (request);
  return FALSE;
}

  static void
clear_timer_and_delete_request (GabbleDiscoRequest *request)
{
  g_assert (request != NULL);

  g_source_remove (request->timer_id);
  delete_request (request);
}

static LmHandlerResult
request_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                  LmMessage *reply_msg, GObject *object, gpointer user_data)
{
  const gchar *namespace;
  const gchar *node;
  GabbleDiscoRequest *request = (GabbleDiscoRequest*) user_data;
  LmMessageNode *query_node, *error_node, *child_node;
  GError *err = NULL;

  g_assert (request);

  query_node = lm_message_node_get_child (reply_msg->node, "query");

  namespace = lm_message_node_get_attribute (query_node, "xmlns");
  if (!namespace)
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }
  node = lm_message_node_get_attribute (query_node, "node");


  if (0 == strcmp (lm_message_node_get_attribute (reply_msg->node, "type"), 
              "error"))
      {
        error_node = lm_message_node_get_child (reply_msg->node, "error");
        child_node = lm_message_node_get_child (error_node, "item-not-found");
        if (child_node)
          {
            err = g_error_new (GABBLE_DISCO_ERROR, GABBLE_DISCO_ERROR_NOT_FOUND,
                               "Item %s was not found",
                               request->jid);
            goto done;
          }
        child_node = lm_message_node_get_child (error_node, 
                                                "service-unavailable");
        if (child_node)
          {
            err = g_error_new (GABBLE_DISCO_ERROR,
                               GABBLE_DISCO_ERROR_UNAVAILABLE,
                               "Service %s was unavailiable",
                               request->jid);
            goto done;
          }
      }
done:
  g_source_remove (request->timer_id);
  request->callback (request->disco, request->jid, node, query_node, err,
                     request->user_data);
  delete_request (request);
  if (err)
    g_error_free (err);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

GabbleDiscoRequest *
gabble_disco_request (GabbleDisco *self, GabbleDiscoType type,
                      const gchar *jid, const char *node,
                      GabbleDiscoCb callback, gpointer user_data,
                      GError **error)
{
  GabbleDiscoPrivate *priv = GABBLE_DISCO_GET_PRIVATE (self);
  GabbleDiscoRequest *request;
  LmMessage *msg;
  LmMessageNode *lm_node;

  request = g_new0 (GabbleDiscoRequest, 1);
  request->disco = self;
  request->type = type;
  request->jid = g_strdup (jid);
  if (node)
    request->node = g_strdup (node);
  request->callback = callback;
  request->user_data = user_data;

  g_debug ("%s: Creating DISCO request %p for %s", 
           G_STRFUNC, request, request->jid);

  priv->requests = g_list_prepend (priv->requests, request);
  msg = lm_message_new_with_sub_type (jid, LM_MESSAGE_TYPE_IQ,
                                           LM_MESSAGE_SUB_TYPE_GET);
  lm_node = lm_message_node_add_child (msg->node, "query", NULL);
  lm_message_node_set_attribute (lm_node, "xmlns", 
                                 "http://jabber.org/protocol/disco#items");
  if (! _gabble_connection_send_with_reply (priv->connection, msg,
        request_reply_cb, G_OBJECT(self), request, error))
    {
      delete_request (request);
      return NULL;
    }
  else
    {
      request->timer_id =
          g_timeout_add (DEFAULT_REQUEST_TIMEOUT, timeout_request, request);

      return request;
    }
}

void
gabble_disco_cancel_request (GabbleDiscoRequest *request)
{
  clear_timer_and_delete_request (request);
}

