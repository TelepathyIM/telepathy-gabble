/*
 * vcard-manager.c - Source for Gabble vCard lookup helper
 *
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#define DBUS_API_SUBJECT_TO_CHANGE

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#define DEBUG_FLAG GABBLE_DEBUG_VCARD

#include "debug.h"
#include "gabble-connection.h"
#include "namespaces.h"
#include "telepathy-helpers.h"
#include "vcard-manager.h"

#define DEFAULT_REQUEST_TIMEOUT 20000

/* signal enum */
enum
{
    NICKNAME_UPDATE,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* Properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

G_DEFINE_TYPE(GabbleVCardManager, gabble_vcard_manager, G_TYPE_OBJECT);

typedef struct _GabbleVCardManagerPrivate GabbleVCardManagerPrivate;
struct _GabbleVCardManagerPrivate
{
  GabbleConnection *connection;
  GList *requests;
  gboolean dispose_has_run;
};

struct _GabbleVCardManagerRequest
{
  GabbleVCardManager *manager;
  guint timer_id;

  GabbleHandle handle;
  gchar **edit_args;

  GabbleVCardManagerCb callback;
  gpointer user_data;
  GObject *bound_object;
};

GQuark
gabble_vcard_manager_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("gabble-vcard-manager-error");
  return quark;
}

GQuark
gabble_vcard_manager_cache_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("gabble-vcard-manager-cache");
  return quark;
}

#define GABBLE_VCARD_MANAGER_GET_PRIVATE(o)     ((GabbleVCardManagerPrivate*)((o)->priv));

static void
gabble_vcard_manager_init (GabbleVCardManager *obj)
{
  GabbleVCardManagerPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_VCARD_MANAGER, GabbleVCardManagerPrivate);
  obj->priv = priv;

}

static void gabble_vcard_manager_set_property (GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void gabble_vcard_manager_get_property (GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);
static void gabble_vcard_manager_dispose (GObject *object);
static void gabble_vcard_manager_finalize (GObject *object);

static void
gabble_vcard_manager_class_init (GabbleVCardManagerClass *gabble_vcard_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_vcard_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_vcard_manager_class, sizeof (GabbleVCardManagerPrivate));

  object_class->get_property = gabble_vcard_manager_get_property;
  object_class->set_property = gabble_vcard_manager_set_property;

  object_class->dispose = gabble_vcard_manager_dispose;
  object_class->finalize = gabble_vcard_manager_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "vCard lookup helper object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  /* signal definitions */

  signals[NICKNAME_UPDATE] =
    g_signal_new ("nickname-update",
                  G_TYPE_FROM_CLASS (gabble_vcard_manager_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

}

static void
gabble_vcard_manager_get_property (GObject    *object,
                                  guint       property_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  GabbleVCardManager *chan = GABBLE_VCARD_MANAGER (object);
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (chan);

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
gabble_vcard_manager_set_property (GObject     *object,
                                  guint        property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  GabbleVCardManager *chan = GABBLE_VCARD_MANAGER (object);
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void cancel_request (GabbleVCardManagerRequest *request);

void
gabble_vcard_manager_dispose (GObject *object)
{
  GabbleVCardManager *self = GABBLE_VCARD_MANAGER (object);
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (self);
  DBusGProxy *bus_proxy;
  bus_proxy = tp_get_bus_proxy ();

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* cancel request removes the element from the list after cancelling */
  while (priv->requests)
    cancel_request (priv->requests->data);

  if (G_OBJECT_CLASS (gabble_vcard_manager_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_vcard_manager_parent_class)->dispose (object);
}

void
gabble_vcard_manager_finalize (GObject *object)
{
  G_OBJECT_CLASS (gabble_vcard_manager_parent_class)->finalize (object);
}


/**
 * gabble_vcard_manager_new:
 * @conn: The #GabbleConnection to use for vCard lookup
 *
 * Creates an object to use for Jabber vCard lookup (JEP 0054).
 * There should be one of these per connection
 */
GabbleVCardManager *
gabble_vcard_manager_new (GabbleConnection *conn)
{
  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);
  return GABBLE_VCARD_MANAGER (g_object_new (GABBLE_TYPE_VCARD_MANAGER, "connection", conn, NULL));
}

static void notify_delete_request (gpointer data, GObject *obj);

static void
delete_request (GabbleVCardManagerRequest *request)
{
  GabbleVCardManager *manager = request->manager;
  GabbleVCardManagerPrivate *priv;

  g_assert (NULL != request);
  g_assert (GABBLE_IS_VCARD_MANAGER (manager));

  priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);

  g_assert (NULL != g_list_find (priv->requests, request));

  priv->requests = g_list_remove (priv->requests, request);

  if (NULL != request->bound_object)
    {
      g_object_weak_unref (request->bound_object, notify_delete_request, request);
    }

  if (0 != request->timer_id)
    {
      g_source_remove (request->timer_id);
    }

  g_free (request);
}

static gboolean
timeout_request (gpointer data)
{
  GabbleVCardManagerRequest *request = (GabbleVCardManagerRequest*) data;
  GError *err;
  g_return_val_if_fail (data != NULL, FALSE);

  err = g_error_new (GABBLE_VCARD_MANAGER_ERROR, GABBLE_VCARD_MANAGER_ERROR_TIMEOUT,
      "Request timed out");
  (request->callback)(request->manager, request, request->handle,
                      NULL, err, request->user_data);
  g_error_free (err);

  request->timer_id = 0;
  delete_request (request);
  return FALSE;
}

static void
cancel_request (GabbleVCardManagerRequest *request)
{
  GError *err;

  g_assert (request != NULL);

  err = g_error_new (GABBLE_VCARD_MANAGER_ERROR, GABBLE_VCARD_MANAGER_ERROR_CANCELLED,
      "Request cancelled");
  (request->callback)(request->manager, request, request->handle,
                      NULL, err, request->user_data);
  g_error_free (err);

  delete_request (request);
}

static void
observe_vcard (GabbleConnection *conn, GabbleVCardManager *manager,
               GabbleHandle handle, LmMessageNode *vcard_node)
{
  LmMessageNode *nick_node = lm_message_node_get_child (vcard_node,
                                                        "NICKNAME");

  if (nick_node)
    {
      const gchar *nick = lm_message_node_get_value (nick_node);

      if (nick && *nick)
        {
          /* nicknames are comma-separated, let's use the first one */
          gchar **bits = g_strsplit (nick, ",", 2);

          if (bits[0])
            {
              gchar *alias = g_strdup (bits[0]);

              g_signal_emit (G_OBJECT (manager), signals[NICKNAME_UPDATE],
                             0, handle);
              if (!gabble_handle_set_qdata (conn->handles,
                                            TP_HANDLE_TYPE_CONTACT,
                                            handle,
                                            gabble_vcard_manager_cache_quark(),
                                            alias, g_free))
                {
                  g_free(alias);
                }
            }

          g_strfreev (bits);
        }
    }
}

static LmHandlerResult
request_reply_cb (GabbleConnection *conn, LmMessage *sent_msg,
                  LmMessage *reply_msg, GObject *object, gpointer user_data)
{
  GabbleVCardManagerRequest *request = (GabbleVCardManagerRequest*) user_data;
  GabbleVCardManager *manager = GABBLE_VCARD_MANAGER (object);
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);
  LmMessageNode *vcard_node;
  GError *err = NULL;

  g_assert (request);

  if (!g_list_find (priv->requests, request))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  vcard_node = lm_message_node_get_child (reply_msg->node, "vCard");

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      LmMessageNode *error_node;

      error_node = lm_message_node_get_child (reply_msg->node, "error");
      if (error_node)
        {
          err = gabble_xmpp_error_to_g_error (
              gabble_xmpp_error_from_node (error_node));
        }

      if (err == NULL)
        {
          err = g_error_new (GABBLE_VCARD_MANAGER_ERROR,
                             GABBLE_VCARD_MANAGER_ERROR_UNKNOWN,
                             "an unknown error occurred");
        }
    }
  else if (NULL == vcard_node)
    {
      err = g_error_new (GABBLE_VCARD_MANAGER_ERROR, GABBLE_VCARD_MANAGER_ERROR_UNKNOWN,
          "vCard lookup response contained no <vCard> node");
    }

  observe_vcard (conn, manager, request->handle, vcard_node);

  if (request->callback)
    {
      request->callback (request->manager, request, request->handle,
                         vcard_node, err, request->user_data);
    }
  delete_request (request);

  if (err)
    g_error_free (err);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
notify_delete_request (gpointer data, GObject *obj)
{
  GabbleVCardManagerRequest *request = (GabbleVCardManagerRequest *) data;
  request->bound_object = NULL;
  delete_request (request);
}

GabbleVCardManagerRequest *
gabble_vcard_manager_request (GabbleVCardManager *self, GabbleHandle handle,
                             guint timeout,
                             GabbleVCardManagerCb callback, gpointer user_data,
                             GObject *object, GError **error)
{
  GabbleVCardManagerPrivate *priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (self);
  GabbleVCardManagerRequest *request;
  LmMessage *msg;
  LmMessageNode *lm_node;
  const gchar *jid;

  if (timeout == 0) timeout = DEFAULT_REQUEST_TIMEOUT;

  request = g_new0 (GabbleVCardManagerRequest, 1);
  request->manager = self;
  request->handle = handle;
  request->callback = callback;
  request->user_data = user_data;
  request->bound_object = object;

  if (NULL != object)
    g_object_weak_ref (object, notify_delete_request, request);

  priv->requests = g_list_prepend (priv->requests, request);
  if (handle == priv->connection->self_handle) 
    {
      jid = NULL;
    }
  else
    {
      jid = gabble_handle_inspect (priv->connection->handles,
                                   TP_HANDLE_TYPE_CONTACT, handle);
    }
  msg = lm_message_new_with_sub_type (jid, LM_MESSAGE_TYPE_IQ,
                                           LM_MESSAGE_SUB_TYPE_GET);
  lm_node = lm_message_node_add_child (msg->node, "query", NULL);
  lm_message_node_set_attribute (lm_node, "xmlns", NS_VCARD_TEMP);

  if (! _gabble_connection_send_with_reply (priv->connection, msg,
        request_reply_cb, G_OBJECT(self), request, error))
    {
      delete_request (request);
      lm_message_unref (msg);
      return NULL;
    }
  else
    {
      request->timer_id =
          g_timeout_add (timeout, timeout_request, request);
      lm_message_unref (msg);
      return request;
    }
}

GabbleVCardManagerRequest *
gabble_vcard_manager_replace (GabbleVCardManager *self,
                             LmMessageNode *replacement, guint timeout,
                             GabbleVCardManagerCb callback, gpointer user_data,
                             GObject *object, GError **error)
{
  return NULL;
}

GabbleVCardManagerRequest *
gabble_vcard_manager_edit (GabbleVCardManager *self,
                          guint timeout,
                          GabbleVCardManagerCb callback, gpointer user_data,
                          GObject *object, GError **error,
                          ...)
{
  va_list ap;
  size_t i, argc;
  GabbleVCardManagerRequest *request;

  request = g_new0 (GabbleVCardManagerRequest, 1);

  argc = 0;
  va_start (ap, error);
  while (va_arg (ap, const gchar *) != NULL)
    {
      argc++;
    }
  va_end (ap);

  request->edit_args = g_new (gchar *, argc + 1);

  va_start (ap, error);
  for (i = 0; i < argc; i++)
    {
      request->edit_args[i] = g_strdup (va_arg (ap, const gchar *));
    }
  request->edit_args[argc] = NULL;
  va_end (ap);

  g_free (request);
  return NULL;
}

void
gabble_vcard_manager_cancel_request (GabbleVCardManager *manager,
                                    GabbleVCardManagerRequest *request)
{
  GabbleVCardManagerPrivate *priv;

  g_return_if_fail (GABBLE_IS_VCARD_MANAGER (manager));
  g_return_if_fail (NULL != request);

  priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);

  g_return_if_fail (NULL != g_list_find (priv->requests, request));

  cancel_request (request);
}

/**
 * Return the cached alias derived from the vCard for the given handle,
 * if any. If there is no cached alias, return NULL.
 */
const gchar *
gabble_vcard_manager_get_cached_alias (GabbleVCardManager *manager,
                                      GabbleHandle handle)
{
  GabbleVCardManagerPrivate *priv;

  g_return_val_if_fail (GABBLE_IS_VCARD_MANAGER (manager), NULL);

  priv = GABBLE_VCARD_MANAGER_GET_PRIVATE (manager);

  return gabble_handle_get_qdata (priv->connection->handles,
                                  TP_HANDLE_TYPE_CONTACT,
                                  handle,
                                  gabble_vcard_manager_cache_quark());
}
