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

G_DEFINE_TYPE_WITH_CODE (GabbleServerTLSChannel, gabble_server_tls_channel,
    TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_SERVER_TLS_CONNECTION,
        NULL));

static void gabble_server_tls_channel_close (TpBaseChannel *base);

static const gchar *gabble_server_tls_channel_interfaces[] = {
  NULL
};

enum {
  /* server TLS channel iface */
  PROP_SERVER_CERTIFICATE = 1,
  PROP_HOSTNAME,
  PROP_REFERENCE_IDENTITIES,

  /* not exported */
  PROP_TLS_SESSION,

  NUM_PROPERTIES
};

struct _GabbleServerTLSChannelPrivate {
  WockyTLSSession *tls_session;

  GabbleTLSCertificate *server_cert;
  gchar *server_cert_path;
  gchar *hostname;
  GStrv reference_identities;

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
    case PROP_SERVER_CERTIFICATE:
      g_value_set_boxed (value, self->priv->server_cert_path);
      break;
    case PROP_HOSTNAME:
      g_value_set_string (value, self->priv->hostname);
      break;
    case PROP_REFERENCE_IDENTITIES:
      g_value_set_boxed (value, self->priv->reference_identities);
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
    case PROP_REFERENCE_IDENTITIES:
      self->priv->reference_identities = g_value_dup_boxed (value);
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
  g_strfreev (self->priv->reference_identities);

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
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (gabble_server_tls_channel_parent_class)->constructed;
  WockyTLSCertType cert_type;
  const gchar *path;
  gchar *cert_object_path;
  GPtrArray *certificates;

  if (chain_up != NULL)
    chain_up (object);

  tp_base_channel_register (base);

  /* create the TLS certificate object */
  path = tp_base_channel_get_object_path (base);
  cert_object_path = g_strdup_printf ("%s/TLSCertificateObject", path);
  certificates = wocky_tls_session_get_peers_certificate (
      self->priv->tls_session, &cert_type);

  self->priv->server_cert = g_object_new (GABBLE_TYPE_TLS_CERTIFICATE,
      "object-path", cert_object_path,
      "certificate-chain-data", certificates,
      "certificate-type", cert_type_to_str (cert_type),
      "dbus-daemon", GABBLE_CONNECTION (base_conn)->daemon,
      NULL);
  self->priv->server_cert_path = cert_object_path;

  DEBUG ("Server TLS channel constructed at %s", path);
}

static void
gabble_server_tls_channel_fill_immutable_properties (
    TpBaseChannel *chan,
    GHashTable *properties)
{
  TP_BASE_CHANNEL_CLASS (gabble_server_tls_channel_parent_class)
      ->fill_immutable_properties (chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
      TP_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION, "ServerCertificate",
      TP_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION, "Hostname",
      TP_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION, "ReferenceIdentities",
      NULL);
}

static gchar *
gabble_server_tls_channel_get_object_path_suffix (TpBaseChannel *base)
{
  return g_strdup ("ServerTLSChannel");
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
    { "ReferenceIdentities", "reference-identities", NULL },
    { NULL }
  };

  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  TpBaseChannelClass *base_class = TP_BASE_CHANNEL_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (GabbleServerTLSChannelPrivate));

  oclass->get_property = gabble_server_tls_channel_get_property;
  oclass->set_property = gabble_server_tls_channel_set_property;
  oclass->dispose = gabble_server_tls_channel_dispose;
  oclass->finalize = gabble_server_tls_channel_finalize;
  oclass->constructed = gabble_server_tls_channel_constructed;

  base_class->channel_type = TP_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION;
  base_class->interfaces = gabble_server_tls_channel_interfaces;
  base_class->target_handle_type = TP_HANDLE_TYPE_NONE;
  base_class->fill_immutable_properties =
      gabble_server_tls_channel_fill_immutable_properties;
  base_class->get_object_path_suffix =
      gabble_server_tls_channel_get_object_path_suffix;
  base_class->close = gabble_server_tls_channel_close;

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

  pspec = g_param_spec_boxed ("reference-identities",
      "The various identities to check the certificate against",
      "The server certificate identity should match one of these identities.",
      G_TYPE_STRV,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_REFERENCE_IDENTITIES, pspec);

  pspec = g_param_spec_object ("tls-session", "The WockyTLSSession",
      "The WockyTLSSession object containing the TLS information",
      WOCKY_TYPE_TLS_SESSION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_TLS_SESSION, pspec);

  tp_dbus_properties_mixin_implement_interface (oclass,
      TP_IFACE_QUARK_CHANNEL_TYPE_SERVER_TLS_CONNECTION,
      tp_dbus_properties_mixin_getter_gobject_properties, NULL,
      server_tls_props);
}

static void
gabble_server_tls_channel_close (TpBaseChannel *base)
{
  DEBUG ("Close() called on the TLS channel %p", base);
  tp_base_channel_destroyed (base);
}

GabbleTLSCertificate *
gabble_server_tls_channel_get_certificate (GabbleServerTLSChannel *self)
{
  return self->priv->server_cert;
}
