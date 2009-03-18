/*
 * bytestream-multiple.c - Source for GabbleBytestreamMultiple
 * Copyright (C) 2007-2008 Collabora Ltd.
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
#include "bytestream-multiple.h"

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_BYTESTREAM

#include "base64.h"
#include "bytestream-factory.h"
#include "bytestream-iface.h"
#include "connection.h"
#include "debug.h"
#include "disco.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"
#include "util.h"

static void
bytestream_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GabbleBytestreamMultiple, gabble_bytestream_multiple,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_BYTESTREAM_IFACE,
      bytestream_iface_init));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_PEER_HANDLE,
  PROP_PEER_HANDLE_TYPE,
  PROP_STREAM_ID,
  PROP_STREAM_INIT_ID,
  PROP_PEER_JID,
  PROP_PEER_RESOURCE,
  PROP_STATE,
  PROP_PROTOCOL,
  PROP_FACTORY,
  PROP_SELF_JID,
  LAST_PROPERTY
};

struct _GabbleBytestreamMultiplePrivate
{
  GabbleConnection *conn;
  TpHandle peer_handle;
  gchar *stream_id;
  gchar *stream_init_id;
  gchar *peer_resource;
  GabbleBytestreamState state;
  gchar *peer_jid;
  GabbleBytestreamFactory *factory;
  gchar *self_full_jid;

  /* List of (gchar *) containing the NS of a stream method */
  GList *fallback_stream_methods;
  GabbleBytestreamIface *active_bytestream;
  gboolean read_blocked;

  gboolean dispose_has_run;
};

#define GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE(obj) ((obj)->priv)

static void bytestream_activate_next (GabbleBytestreamMultiple *self);

static void
gabble_bytestream_multiple_init (GabbleBytestreamMultiple *self)
{
  GabbleBytestreamMultiplePrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_BYTESTREAM_MULTIPLE, GabbleBytestreamMultiplePrivate);

  self->priv = priv;
}

static void
gabble_bytestream_multiple_dispose (GObject *object)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (object);
  GabbleBytestreamMultiplePrivate *priv =
      GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_handle_unref (contact_repo, priv->peer_handle);

  if (priv->state != GABBLE_BYTESTREAM_STATE_CLOSED)
    {
      gabble_bytestream_iface_close (GABBLE_BYTESTREAM_IFACE (self), NULL);
    }

  G_OBJECT_CLASS (gabble_bytestream_multiple_parent_class)->dispose (object);
}

static void
gabble_bytestream_multiple_finalize (GObject *object)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (object);
  GabbleBytestreamMultiplePrivate *priv =
      GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);
  GList *l;

  for (l = priv->fallback_stream_methods; l != NULL; l = g_list_next (l))
    g_free (l->data);
  g_list_free (priv->fallback_stream_methods);

  g_free (priv->stream_id);
  g_free (priv->stream_init_id);
  g_free (priv->peer_resource);
  g_free (priv->peer_jid);
  g_free (priv->self_full_jid);

  G_OBJECT_CLASS (gabble_bytestream_multiple_parent_class)->finalize (object);
}

static void
gabble_bytestream_multiple_get_property (GObject *object,
                                         guint property_id,
                                         GValue *value,
                                         GParamSpec *pspec)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (object);
  GabbleBytestreamMultiplePrivate *priv =
      GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_PEER_HANDLE:
        g_value_set_uint (value, priv->peer_handle);
        break;
      case PROP_PEER_HANDLE_TYPE:
        g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
        break;
      case PROP_STREAM_ID:
        g_value_set_string (value, priv->stream_id);
        break;
      case PROP_STREAM_INIT_ID:
        g_value_set_string (value, priv->stream_init_id);
        break;
      case PROP_PEER_RESOURCE:
        g_value_set_string (value, priv->peer_resource);
        break;
      case PROP_PEER_JID:
        g_value_set_string (value, priv->peer_jid);
        break;
      case PROP_STATE:
        g_value_set_uint (value, priv->state);
        break;
      case PROP_PROTOCOL:
        g_value_set_string (value, NS_BYTESTREAMS);
        break;
      case PROP_FACTORY:
        g_value_set_object (value, priv->factory);
        break;
      case PROP_SELF_JID:
        g_value_set_string (value, priv->self_full_jid);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_bytestream_multiple_set_property (GObject *object,
                                         guint property_id,
                                         const GValue *value,
                                         GParamSpec *pspec)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (object);
  GabbleBytestreamMultiplePrivate *priv =
      GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_PEER_HANDLE:
        priv->peer_handle = g_value_get_uint (value);
        break;
      case PROP_STREAM_ID:
        g_free (priv->stream_id);
        priv->stream_id = g_value_dup_string (value);
        break;
      case PROP_STREAM_INIT_ID:
        g_free (priv->stream_init_id);
        priv->stream_init_id = g_value_dup_string (value);
        break;
      case PROP_PEER_RESOURCE:
        g_free (priv->peer_resource);
        priv->peer_resource = g_value_dup_string (value);
        break;
      case PROP_STATE:
        if (priv->state != g_value_get_uint (value))
          {
            priv->state = g_value_get_uint (value);
            g_signal_emit_by_name (object, "state-changed", priv->state);
          }
        break;
      case PROP_FACTORY:
        priv->factory = g_value_get_object (value);
        break;
      case PROP_SELF_JID:
        g_free (priv->self_full_jid);
        priv->self_full_jid = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
gabble_bytestream_multiple_constructor (GType type,
                                        guint n_props,
                                        GObjectConstructParam *props)
{
  GObject *obj;
  GabbleBytestreamMultiplePrivate *priv;
  TpHandleRepoIface *contact_repo;
  const gchar *jid;

  obj = G_OBJECT_CLASS (gabble_bytestream_multiple_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (
      GABBLE_BYTESTREAM_MULTIPLE (obj));

  g_assert (priv->conn != NULL);
  g_assert (priv->peer_handle != 0);
  g_assert (priv->stream_id != NULL);

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  tp_handle_ref (contact_repo, priv->peer_handle);

  jid = tp_handle_inspect (contact_repo, priv->peer_handle);

  if (priv->peer_resource != NULL)
    priv->peer_jid = g_strdup_printf ("%s/%s", jid, priv->peer_resource);
  else
    priv->peer_jid = g_strdup (jid);

  g_assert (priv->self_full_jid != NULL);

  return obj;
}

static void
gabble_bytestream_multiple_class_init (
    GabbleBytestreamMultipleClass *gabble_bytestream_multiple_class)
{
  GObjectClass *object_class =
      G_OBJECT_CLASS (gabble_bytestream_multiple_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_bytestream_multiple_class,
      sizeof (GabbleBytestreamMultiplePrivate));

  object_class->dispose = gabble_bytestream_multiple_dispose;
  object_class->finalize = gabble_bytestream_multiple_finalize;

  object_class->get_property = gabble_bytestream_multiple_get_property;
  object_class->set_property = gabble_bytestream_multiple_set_property;
  object_class->constructor = gabble_bytestream_multiple_constructor;

   g_object_class_override_property (object_class, PROP_CONNECTION,
      "connection");
   g_object_class_override_property (object_class, PROP_PEER_HANDLE,
       "peer-handle");
   g_object_class_override_property (object_class, PROP_PEER_HANDLE_TYPE,
       "peer-handle-type");
   g_object_class_override_property (object_class, PROP_STREAM_ID,
       "stream-id");
   g_object_class_override_property (object_class, PROP_PEER_JID,
       "peer-jid");
   g_object_class_override_property (object_class, PROP_STATE,
       "state");
   g_object_class_override_property (object_class, PROP_PROTOCOL,
       "protocol");

  param_spec = g_param_spec_string (
      "peer-resource",
      "Peer resource",
      "the resource used by the remote peer during the SI, if any",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PEER_RESOURCE,
      param_spec);

  param_spec = g_param_spec_string (
      "stream-init-id",
      "stream init ID",
      "the iq ID of the SI request, if any",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STREAM_INIT_ID,
      param_spec);

  param_spec = g_param_spec_object (
      "factory",
      "Factory",
      "The GabbleBytestreamFactory that created the stream",
      GABBLE_TYPE_BYTESTREAM_FACTORY,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_FACTORY,
      param_spec);

  param_spec = g_param_spec_string (
      "self-jid",
      "Our self jid",
      "Either a contact full jid or a muc jid",
      NULL,
      G_PARAM_CONSTRUCT_ONLY  | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SELF_JID,
      param_spec);
}

/*
 * gabble_bytestream_multiple_send
 *
 * Implements gabble_bytestream_iface_send on GabbleBytestreamIface
 */
static gboolean
gabble_bytestream_multiple_send (GabbleBytestreamIface *iface,
                                 guint len,
                                 const gchar *str)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (iface);
  GabbleBytestreamMultiplePrivate *priv =
      GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  if (priv->state != GABBLE_BYTESTREAM_STATE_OPEN)
    {
      DEBUG ("can't send data through a not open bytestream (state: %d)",
          priv->state);
      return FALSE;
    }

  g_assert (priv->active_bytestream != NULL);

  return gabble_bytestream_iface_send (priv->active_bytestream, len, str);
}

/*
 * gabble_bytestream_multiple_accept
 *
 * Implements gabble_bytestream_iface_accept on GabbleBytestreamIface
 */
static void
gabble_bytestream_multiple_accept (GabbleBytestreamIface *iface,
                                   GabbleBytestreamAugmentSiAcceptReply func,
                                   gpointer user_data)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (iface);
  GabbleBytestreamMultiplePrivate *priv =
      GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);
  LmMessage *msg;
  LmMessageNode *si;
  GList *all_methods;
  gchar *current_method;

  /* We cannot just call the accept method of the active bytestream because
   * the result stanza is different if we are using si-multiple */

  if (priv->state != GABBLE_BYTESTREAM_STATE_LOCAL_PENDING)
    {
      /* The stream was previoulsy or automatically accepted */
      return;
    }

  g_return_if_fail (priv->active_bytestream != NULL);

  all_methods = g_list_copy (priv->fallback_stream_methods);
  g_object_get (priv->active_bytestream, "protocol", &current_method, NULL);
  all_methods = g_list_prepend (all_methods, current_method);

  msg = gabble_bytestream_factory_make_multi_accept_iq (priv->peer_jid,
      priv->stream_init_id, all_methods);

  g_free (current_method);
  g_list_free (all_methods);

  si = lm_message_node_get_child_with_namespace (msg->node, "si", NS_SI);
  g_assert (si != NULL);

  if (func != NULL)
    {
      /* let the caller add his profile specific data */
      func (si, user_data);
    }

  if (_gabble_connection_send (priv->conn, msg, NULL))
    {
      DEBUG ("stream %s with %s is now accepted", priv->stream_id,
          priv->peer_jid);
      g_object_set (priv->active_bytestream, "state",
          GABBLE_BYTESTREAM_STATE_ACCEPTED, NULL);
    }

  lm_message_unref (msg);
}


/*
 * gabble_bytestream_multiple_close
 *
 * Implements gabble_bytestream_iface_close on GabbleBytestreamIface
 */
static void
gabble_bytestream_multiple_close (GabbleBytestreamIface *iface,
                                  GError *error)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (iface);
  GabbleBytestreamMultiplePrivate *priv =
      GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  if (priv->state == GABBLE_BYTESTREAM_STATE_CLOSED)
     /* bytestream already closed, do nothing */
     return;

  if (priv->active_bytestream != NULL)
    gabble_bytestream_iface_close (priv->active_bytestream, error);
  else
    /* It can happen if the bytestream is still empty, i.e. not stream
     * methods have been added yet */
    g_object_set (self, "state", GABBLE_BYTESTREAM_STATE_CLOSED, NULL);
}

/*
 * gabble_bytestream_multiple_initiate
 *
 * Implements gabble_bytestream_iface_initiate on GabbleBytestreamIface
 */
static gboolean
gabble_bytestream_multiple_initiate (GabbleBytestreamIface *iface)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (iface);
  GabbleBytestreamMultiplePrivate *priv =
      GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  if (priv->state != GABBLE_BYTESTREAM_STATE_INITIATING)
    {
      DEBUG ("bytestream is not is the initiating state (state %d)",
          priv->state);
      return FALSE;
    }

  if (priv->active_bytestream == NULL)
    {
      DEBUG ("no bytestreams to initiate");
      return FALSE;
    }

  return gabble_bytestream_iface_initiate (priv->active_bytestream);
}

static void
bytestream_data_received_cb (GabbleBytestreamIface *bytestream,
                             TpHandle sender,
                             GString *str,
                             gpointer user_data)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (user_data);

  /* Just forward the data */
  g_signal_emit_by_name (G_OBJECT (self), "data-received", sender, str);
}

static void
bytestream_state_changed_cb (GabbleBytestreamIface *bytestream,
                             GabbleBytestreamState state,
                             gpointer user_data)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (user_data);

  /* When there is a connection error the state of the sub-bytestream becomes
   * CLOSED. There is no risk to receive a notification for this kind of
   * change because the signal handler is previously disconnected in
   * bytestream_connection_error_cb */

  g_object_set (self, "state", state, NULL);
}

static void
bytestream_write_blocked_cb (GabbleBytestreamIface *bytestream,
                             gboolean blocked,
                             gpointer self)
{
  /* Forward signal */
  g_signal_emit_by_name (G_OBJECT (self), "write-blocked", blocked);
}

static void
bytestream_connection_error_cb (GabbleBytestreamIface *failed,
                                gpointer user_data)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (user_data);
  GabbleBytestreamMultiplePrivate *priv =
      GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  g_assert (failed == priv->active_bytestream);
  /* the error signal is only emitted when intiating the bytestream */
  g_assert (priv->state == GABBLE_BYTESTREAM_STATE_INITIATING ||
      priv->state == GABBLE_BYTESTREAM_STATE_ACCEPTED);

  g_signal_handlers_disconnect_by_func (failed,
      bytestream_connection_error_cb, self);
  g_signal_handlers_disconnect_by_func (failed,
      bytestream_data_received_cb, self);
  g_signal_handlers_disconnect_by_func (failed,
      bytestream_state_changed_cb, self);
  g_signal_handlers_disconnect_by_func (failed,
      bytestream_write_blocked_cb, self);

  /* We don't have to unref it because the reference is kept by the
   * factory */
  priv->active_bytestream = NULL;

  if (priv->fallback_stream_methods == NULL)
    return;

  DEBUG ("Trying alternative streaming method");

  bytestream_activate_next (self);

  if (priv->state == GABBLE_BYTESTREAM_STATE_INITIATING)
    /* The previous bytestream failed when initiating it, so now we have to
     * initiate the new one */
    gabble_bytestream_iface_initiate (priv->active_bytestream);
}

static void
bytestream_activate_next (GabbleBytestreamMultiple *self)
{
  GabbleBytestreamMultiplePrivate *priv =
      GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);
  gchar *stream_method;

  g_return_if_fail (priv->active_bytestream == NULL);
  /* The caller has to be sure that there is a fallback method */
  g_return_if_fail (priv->fallback_stream_methods != NULL);

  /* Try the first stream method in the fallback list */
  stream_method = priv->fallback_stream_methods->data;
  priv->fallback_stream_methods = g_list_delete_link (
      priv->fallback_stream_methods, priv->fallback_stream_methods);

  priv->active_bytestream = gabble_bytestream_factory_create_from_method (
      priv->factory, stream_method, priv->peer_handle, priv->stream_id,
      priv->stream_init_id, priv->peer_resource, priv->self_full_jid,
      priv->state);

  /* Methods have already been checked so this shouldn't fail */
  g_assert (priv->active_bytestream != NULL);

  g_free (stream_method);

  /* block the new bytestream if needed */
  gabble_bytestream_iface_block_reading (priv->active_bytestream,
      priv->read_blocked);

  g_signal_connect (priv->active_bytestream, "connection-error",
      G_CALLBACK (bytestream_connection_error_cb), self);
  g_signal_connect (priv->active_bytestream, "data-received",
      G_CALLBACK (bytestream_data_received_cb), self);
  g_signal_connect (priv->active_bytestream, "state-changed",
      G_CALLBACK (bytestream_state_changed_cb), self);
  g_signal_connect (priv->active_bytestream, "write-blocked",
      G_CALLBACK (bytestream_write_blocked_cb), self);
}

/*
 * gabble_bytestream_multiple_add_bytestream
 *
 * Add an alternative stream method.
 */
void
gabble_bytestream_multiple_add_stream_method (GabbleBytestreamMultiple *self,
                                              const gchar *method)
{
  GabbleBytestreamMultiplePrivate *priv;

  g_return_if_fail (GABBLE_IS_BYTESTREAM_MULTIPLE (self));
  g_return_if_fail (method != NULL);

  priv = GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  DEBUG ("Add bytestream method %s", method);

  priv->fallback_stream_methods = g_list_append (
      priv->fallback_stream_methods, g_strdup (method));

  if (priv->active_bytestream == NULL)
    bytestream_activate_next (self);
}

gboolean
gabble_bytestream_multiple_has_stream_method (GabbleBytestreamMultiple *self)
{
  GabbleBytestreamMultiplePrivate *priv =
    GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  if (priv->active_bytestream != NULL)
    return TRUE;

  return (g_list_length (priv->fallback_stream_methods) != 0);
}

static void
gabble_bytestream_multiple_block_reading (GabbleBytestreamIface *iface,
                                          gboolean block)
{
  GabbleBytestreamMultiple *self = GABBLE_BYTESTREAM_MULTIPLE (iface);
  GabbleBytestreamMultiplePrivate *priv =
    GABBLE_BYTESTREAM_MULTIPLE_GET_PRIVATE (self);

  if (priv->read_blocked == block)
    return;

  priv->read_blocked = block;

  g_assert (priv->active_bytestream != NULL);
  gabble_bytestream_iface_block_reading (priv->active_bytestream, block);
}

static void
bytestream_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  GabbleBytestreamIfaceClass *klass = (GabbleBytestreamIfaceClass *) g_iface;

  klass->initiate = gabble_bytestream_multiple_initiate;
  klass->send = gabble_bytestream_multiple_send;
  klass->close = gabble_bytestream_multiple_close;
  klass->accept = gabble_bytestream_multiple_accept;
  klass->block_reading = gabble_bytestream_multiple_block_reading;
}
