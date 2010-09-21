/*
 * call-content-codecoffer.c - Source for GabbleCallContentCodecoffer
 * Copyright (C) 2009 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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


#include <stdio.h>
#include <stdlib.h>

#include <telepathy-glib/telepathy-glib.h>

#include "call-content-codecoffer.h"
#include <extensions/extensions.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA
#include "debug.h"

static void call_content_codecoffer_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(GabbleCallContentCodecoffer,
  gabble_call_content_codecoffer,
  G_TYPE_OBJECT,
  G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CALL_CONTENT_CODEC_OFFER,
        call_content_codecoffer_iface_init);
   G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
    tp_dbus_properties_mixin_iface_init);
  );

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_DBUS_DAEMON,
  PROP_REMOTE_CONTACT_CODEC_MAP,
};

/* private structure */
struct _GabbleCallContentCodecofferPrivate
{
  gboolean dispose_has_run;

  TpDBusDaemon *dbus_daemon;
  gchar *object_path;
  GHashTable *codec_map;
  GSimpleAsyncResult *result;
  GCancellable *cancellable;
  guint handler_id;
};

#define GABBLE_CALL_CONTENT_CODECOFFER_GET_PRIVATE(o) \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
    GABBLE_TYPE_CALL_CONTENT_CODECOFFER, GabbleCallContentCodecofferPrivate))

static void
gabble_call_content_codecoffer_init (GabbleCallContentCodecoffer *self)
{
  GabbleCallContentCodecofferPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CALL_CONTENT_CODECOFFER,
      GabbleCallContentCodecofferPrivate);

  self->priv = priv;
}

static void gabble_call_content_codecoffer_dispose (GObject *object);
static void gabble_call_content_codecoffer_finalize (GObject *object);

static void
gabble_call_content_codecoffer_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  GabbleCallContentCodecoffer *offer =
    GABBLE_CALL_CONTENT_CODECOFFER (object);
  GabbleCallContentCodecofferPrivate *priv = offer->priv;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_REMOTE_CONTACT_CODEC_MAP:
        g_value_set_boxed (value, priv->codec_map);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_call_content_codecoffer_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleCallContentCodecoffer *content =
    GABBLE_CALL_CONTENT_CODECOFFER (object);
  GabbleCallContentCodecofferPrivate *priv = content->priv;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        priv->object_path = g_value_dup_string (value);
        g_assert (priv->object_path != NULL);
        break;
      case PROP_REMOTE_CONTACT_CODEC_MAP:
        priv->codec_map = g_value_dup_boxed (value);
        break;
      case PROP_DBUS_DAEMON:
        g_assert (priv->dbus_daemon == NULL);   /* construct-only */
        priv->dbus_daemon = g_value_dup_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_call_content_codecoffer_class_init (
  GabbleCallContentCodecofferClass *gabble_call_content_codecoffer_class)
{
  GObjectClass *object_class =
    G_OBJECT_CLASS (gabble_call_content_codecoffer_class);
  GParamSpec *spec;

  static TpDBusPropertiesMixinPropImpl codecoffer_props[] = {
    { "RemoteContactCodecMap", "remote-contact-codec-map", NULL },
    { NULL }
  };

  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { GABBLE_IFACE_CALL_CONTENT_CODEC_OFFER,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        codecoffer_props,
      },
      { NULL }
  };


  g_type_class_add_private (gabble_call_content_codecoffer_class,
    sizeof (GabbleCallContentCodecofferPrivate));

  object_class->get_property = gabble_call_content_codecoffer_get_property;
  object_class->set_property = gabble_call_content_codecoffer_set_property;

  object_class->dispose = gabble_call_content_codecoffer_dispose;
  object_class->finalize = gabble_call_content_codecoffer_finalize;

  spec = g_param_spec_string ("object-path", "D-Bus object path",
      "The D-Bus object path used for this "
      "object on the bus.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, spec);

  spec = g_param_spec_boxed ("remote-contact-codec-map",
      "RemoteContactCodecMap",
      "The map of contacts to codecs",
      GABBLE_HASH_TYPE_CONTACT_CODEC_MAP,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REMOTE_CONTACT_CODEC_MAP,
      spec);

  spec = g_param_spec_object ("dbus-daemon",
      "The DBus daemon connection",
      "The connection to the DBus daemon owning the CM",
      TP_TYPE_DBUS_DAEMON,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DBUS_DAEMON, spec);

  gabble_call_content_codecoffer_class->dbus_props_class.interfaces
    = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleCallContentCodecofferClass, dbus_props_class));
}

void
gabble_call_content_codecoffer_dispose (GObject *object)
{
  GabbleCallContentCodecoffer *self = GABBLE_CALL_CONTENT_CODECOFFER (object);
  GabbleCallContentCodecofferPrivate *priv = self->priv;

  g_assert (priv->result == NULL);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_clear_object (&priv->dbus_daemon);

  if (priv->codec_map != NULL)
    {
      /* dbus-glib :( */
      g_boxed_free (GABBLE_HASH_TYPE_CONTACT_CODEC_MAP, priv->codec_map);
    }
  priv->codec_map = NULL;

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (gabble_call_content_codecoffer_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_content_codecoffer_parent_class)->dispose (
      object);
}

void
gabble_call_content_codecoffer_finalize (GObject *object)
{
  GabbleCallContentCodecoffer *self = GABBLE_CALL_CONTENT_CODECOFFER (object);
  GabbleCallContentCodecofferPrivate *priv = self->priv;

  g_free (priv->object_path);
  /* free any data held directly by the object here */

  G_OBJECT_CLASS (gabble_call_content_codecoffer_parent_class)->finalize (
    object);
}

static void
gabble_call_content_codec_offer_accept (GabbleSvcCallContentCodecOffer *iface,
    const GPtrArray *codecs,
    DBusGMethodInvocation *context)
{
  GabbleCallContentCodecoffer *self = GABBLE_CALL_CONTENT_CODECOFFER (iface);
  GabbleCallContentCodecofferPrivate *priv = self->priv;

  DEBUG ("%s was accepted", priv->object_path);

  if (priv->cancellable != NULL)
    {
      g_cancellable_disconnect (priv->cancellable, priv->handler_id);
      priv->handler_id = 0;
    }

  tp_clear_object (&priv->cancellable);

  g_simple_async_result_set_op_res_gpointer (priv->result,
    (gpointer) codecs, NULL);
  g_simple_async_result_complete (priv->result);
  tp_clear_object (&priv->result);

  gabble_svc_call_content_codec_offer_return_from_accept (context);

  tp_dbus_daemon_unregister_object (priv->dbus_daemon, G_OBJECT (self));
}

static void
gabble_call_content_codec_offer_reject (GabbleSvcCallContentCodecOffer *iface,
    DBusGMethodInvocation *context)
{
  GabbleCallContentCodecoffer *self = GABBLE_CALL_CONTENT_CODECOFFER (iface);
  GabbleCallContentCodecofferPrivate *priv = self->priv;

  DEBUG ("%s was rejected", priv->object_path);

  if (priv->cancellable != NULL)
    {
      g_cancellable_disconnect (priv->cancellable, priv->handler_id);
      priv->handler_id = 0;
    }

  tp_clear_object (&priv->cancellable);

  g_simple_async_result_set_error (priv->result,
      G_IO_ERROR, G_IO_ERROR_FAILED, "Codec offer was rejected");
  g_simple_async_result_complete (priv->result);
  tp_clear_object (&priv->result);

  gabble_svc_call_content_codec_offer_return_from_reject (context);

  tp_dbus_daemon_unregister_object (priv->dbus_daemon, G_OBJECT (self));
}

static void
call_content_codecoffer_iface_init (gpointer iface, gpointer data)
{
  GabbleSvcCallContentCodecOfferClass *klass =
    (GabbleSvcCallContentCodecOfferClass *) iface;

#define IMPLEMENT(x) gabble_svc_call_content_codec_offer_implement_##x (\
    klass, gabble_call_content_codec_offer_##x)
  IMPLEMENT(accept);
  IMPLEMENT(reject);
#undef IMPLEMENT
}

GabbleCallContentCodecoffer *
gabble_call_content_codecoffer_new (TpDBusDaemon *dbus_daemon,
    const gchar *object_path,
    GHashTable *codecs)
{
  return g_object_new (GABBLE_TYPE_CALL_CONTENT_CODECOFFER,
      "dbus-daemon", dbus_daemon,
      "object-path", object_path,
      "remote-contact-codec-map", codecs,
      NULL);
}

static void
cancelled_cb (GCancellable *cancellable, gpointer user_data)
{
  GabbleCallContentCodecoffer *offer = user_data;
  GabbleCallContentCodecofferPrivate *priv = offer->priv;

  tp_dbus_daemon_unregister_object (priv->dbus_daemon, G_OBJECT (offer));

  g_simple_async_result_set_error (priv->result,
      G_IO_ERROR, G_IO_ERROR_CANCELLED, "Offer cancelled");
  g_simple_async_result_complete_in_idle (priv->result);

  tp_clear_object (&priv->cancellable);
  tp_clear_object (&priv->result);
  priv->handler_id = 0;
}

void
gabble_call_content_codecoffer_offer (GabbleCallContentCodecoffer *offer,
  GCancellable *cancellable,
  GAsyncReadyCallback callback,
  gpointer user_data)
{
  GabbleCallContentCodecofferPrivate *priv = offer->priv;

  /* FIXME implement cancellable support */
  if (G_UNLIKELY (priv->result != NULL))
    goto pending;

  priv->result = g_simple_async_result_new (G_OBJECT (offer),
    callback, user_data, gabble_call_content_codecoffer_offer_finish);

  /* register object on the bus */
  DEBUG ("Registering %s", priv->object_path);
  tp_dbus_daemon_register_object (priv->dbus_daemon, priv->object_path,
      G_OBJECT (offer));

  if (cancellable != NULL)
    {
      priv->cancellable = cancellable;
      priv->handler_id = g_cancellable_connect (
          cancellable, G_CALLBACK (cancelled_cb), offer, NULL);
    }

  return;

pending:
  g_simple_async_report_error_in_idle (G_OBJECT (offer), callback, user_data,
    G_IO_ERROR, G_IO_ERROR_PENDING, "Another offer operation is pending");
}

GPtrArray *
gabble_call_content_codecoffer_offer_finish (
  GabbleCallContentCodecoffer *offer,
  GAsyncResult *result,
  GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
      error))
    return FALSE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
    G_OBJECT (offer), gabble_call_content_codecoffer_offer_finish),
    NULL);

  return g_simple_async_result_get_op_res_gpointer (
    G_SIMPLE_ASYNC_RESULT (result));
}
