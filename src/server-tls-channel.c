/*
 * server-tls-channel.c - Source for GabbleServerTLSChannel
 * Copyright (C) 2010 Collabora Ltd.
 * @author Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
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

#include <config.h>

#include "server-tls-channel.h"

#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>

#include <wocky/wocky-tls.h>

#define DEBUG_FLAG GABBLE_DEBUG_TLS
#include "debug.h"
#include "connection.h"
#include "tls-certificate.h"

static void channel_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleServerTLSChannel, gabble_server_tls_channel,
    GABBLE_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
        tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CHANNEL_TYPE_SERVER_TLS_CONNECTION,
        NULL));

static const gchar *gabble_server_tls_channel_interfaces[] = {
  NULL
};

enum {
  /* channel iface */
  PROP_CHANNEL_PROPERTIES = 1,
  PROP_REQUESTED,

  /* server TLS channel iface */
  PROP_SERVER_CERTIFICATE,
  PROP_HOSTNAME,

  /* not exported */
  PROP_TLS_SESSION,

  NUM_PROPERTIES
};

struct _GabbleServerTLSChannelPrivate {
  WockyTLSSession *tls_session;

  GabbleTLSCertificate *server_cert;
  gchar *server_cert_path;
  gchar *hostname;

  gboolean dispose_has_run;
};

static void
gabble_server_tls_channel_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  GabbleServerTLSChannel *self = GABBLE_SERVER_TLS_CHANNEL (object);

  switch (property_id)
    {
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
              GABBLE_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION,
              "ServerCertificate",
              GABBLE_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION,
              "Hostname",
              NULL));
      break;
    case PROP_REQUESTED:
      g_value_set_boolean (value, FALSE);
      break;
    case PROP_SERVER_CERTIFICATE:
      g_value_set_boxed (value, self->priv->server_cert_path);
      break;
    case PROP_HOSTNAME:
      g_value_set_string (value, self->priv->hostname);
      break;
    case PROP_TLS_SESSION:
      g_value_set_object (value, self->priv->tls_session);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gabble_server_tls_channel_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleServerTLSChannel *self = GABBLE_SERVER_TLS_CHANNEL (object);

  switch (property_id)
    {
    case PROP_TLS_SESSION:
      self->priv->tls_session = g_value_dup_object (value);
      break;
    case PROP_HOSTNAME:
      self->priv->hostname = g_value_dup_string (value);
      break;
    case PROP_REQUESTED:
      /* no-op */
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gabble_server_tls_channel_finalize (GObject *object)
{
  GabbleServerTLSChannel *self = GABBLE_SERVER_TLS_CHANNEL (object);

  DEBUG ("Finalize TLS channel");

  g_free (self->priv->server_cert_path);
  g_free (self->priv->hostname);

  G_OBJECT_CLASS (gabble_server_tls_channel_parent_class)->finalize (object);
}

static void
gabble_server_tls_channel_dispose (GObject *object)
{
  GabbleServerTLSChannel *self = GABBLE_SERVER_TLS_CHANNEL (object);

  if (self->priv->dispose_has_run)
    return;

  DEBUG ("Dispose TLS channel");

  self->priv->dispose_has_run = TRUE;

  gabble_server_tls_channel_close (self);

  tp_clear_object (&self->priv->server_cert);
  tp_clear_object (&self->priv->tls_session);

  G_OBJECT_CLASS (gabble_server_tls_channel_parent_class)->dispose (object);
}

static const gchar *
cert_type_to_str (WockyTLSCertType type)
{
  const gchar *retval = NULL;

  switch (type)
    {
    case WOCKY_TLS_CERT_TYPE_X509:
      retval = "x509";
      break;
    case WOCKY_TLS_CERT_TYPE_OPENPGP:
      retval = "pgp";
      break;
    default:
      break;
    }

  return retval;
}

static void
gabble_server_tls_channel_constructed (GObject *object)
{
  GabbleServerTLSChannel *self = GABBLE_SERVER_TLS_CHANNEL (object);
  GabbleBaseChannel *base = GABBLE_BASE_CHANNEL (self);
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (gabble_server_tls_channel_parent_class)->constructed;
  WockyTLSCertType cert_type;
  gchar *cert_object_path;
  GPtrArray *certificates;

  if (chain_up != NULL)
    chain_up (object);

  /* put the channel on the bus */
  gabble_base_channel_register (base);

  /* create the TLS certificate object */
  cert_object_path = g_strdup_printf ("%s/TLSCertificateObject",
      base->object_path);
  certificates = wocky_tls_session_get_peers_certificate
    (self->priv->tls_session, &cert_type);

  self->priv->server_cert = g_object_new (GABBLE_TYPE_TLS_CERTIFICATE,
      "object-path", cert_object_path,
      "certificate-chain-data", certificates,
      "certificate-type", cert_type_to_str (cert_type),
      "dbus-daemon", base->conn->daemon,
      NULL);
  self->priv->server_cert_path = cert_object_path;

  DEBUG ("Server TLS channel constructed at %s", base->object_path);
}

static void
gabble_server_tls_channel_init (GabbleServerTLSChannel *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_SERVER_TLS_CHANNEL, GabbleServerTLSChannelPrivate);
}

static void
gabble_server_tls_channel_class_init (GabbleServerTLSChannelClass *klass)
{
  static TpDBusPropertiesMixinPropImpl server_tls_props[] = {
    { "ServerCertificate", "server-certificate", NULL },
    { "Hostname", "hostname", NULL },
    { NULL }
  };

  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GabbleBaseChannelClass *base_class = GABBLE_BASE_CHANNEL_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (GabbleServerTLSChannelPrivate));

  oclass->get_property = gabble_server_tls_channel_get_property;
  oclass->set_property = gabble_server_tls_channel_set_property;
  oclass->dispose = gabble_server_tls_channel_dispose;
  oclass->finalize = gabble_server_tls_channel_finalize;
  oclass->constructed = gabble_server_tls_channel_constructed;

  base_class->channel_type = GABBLE_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION;
  base_class->interfaces = gabble_server_tls_channel_interfaces;
  base_class->target_type = TP_HANDLE_TYPE_NONE;

  /* channel iface */
  g_object_class_override_property (oclass, PROP_CHANNEL_PROPERTIES,
      "channel-properties");
  g_object_class_override_property (oclass, PROP_REQUESTED,
      "requested");

  pspec = g_param_spec_boxed ("server-certificate", "Server certificate path",
      "The object path of the server certificate.",
      DBUS_TYPE_G_OBJECT_PATH,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_SERVER_CERTIFICATE, pspec);

  pspec = g_param_spec_string ("hostname", "The hostname to be verified",
      "The hostname which should be certified by the server certificate.",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_HOSTNAME, pspec);

  pspec = g_param_spec_object ("tls-session", "The WockyTLSSession",
      "The WockyTLSSession object containing the TLS information",
      WOCKY_TYPE_TLS_SESSION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_TLS_SESSION, pspec);

  tp_dbus_properties_mixin_implement_interface (oclass,
      GABBLE_IFACE_QUARK_CHANNEL_TYPE_SERVER_TLS_CONNECTION,
      tp_dbus_properties_mixin_getter_gobject_properties, NULL,
      server_tls_props);
}

static void
gabble_server_tls_channel_impl_close (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  GabbleServerTLSChannel *self = GABBLE_SERVER_TLS_CHANNEL (iface);

  g_assert (GABBLE_IS_SERVER_TLS_CHANNEL (self));

  gabble_server_tls_channel_close (self);
  tp_svc_channel_return_from_close (context);
}

static void
channel_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, gabble_server_tls_channel_impl_##x)
  IMPLEMENT(close);
#undef IMPLEMENT
}

void
gabble_server_tls_channel_close (GabbleServerTLSChannel *self)
{
  GabbleBaseChannel *base = GABBLE_BASE_CHANNEL (self);

  if (base->closed)
    return;

  base->closed = TRUE;

  DEBUG ("Close() called on the TLS channel %p", self);
  tp_svc_channel_emit_closed (self);
}

GabbleTLSCertificate *
gabble_server_tls_channel_get_certificate (GabbleServerTLSChannel *self)
{
  return self->priv->server_cert;
}
