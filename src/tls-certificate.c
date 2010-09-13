/*
 * tls-certificate.c - Source for GabbleTLSCertificate
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

#include "config.h"
#include "tls-certificate.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG GABBLE_DEBUG_TLS
#include "debug.h"

#include "extensions/extensions.h"

static void
tls_certificate_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GabbleTLSCertificate,
    gabble_tls_certificate,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_AUTHENTICATION_TLS_CERTIFICATE,
        tls_certificate_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
        tp_dbus_properties_mixin_iface_init);)

struct _GabbleTLSCertificatePrivate {
  gchar *object_path;

  gchar *cert_type;
  GabbleTLSCertificateState cert_state;

  GPtrArray *rejections;
  GPtrArray *cert_data;

  TpDBusDaemon *daemon;

  gboolean dispose_has_run;
};

enum {
  PROP_OBJECT_PATH = 1,
  PROP_STATE,
  PROP_REJECTIONS,
  PROP_CERTIFICATE_TYPE,
  PROP_CERTIFICATE_CHAIN_DATA,

  /* not exported */
  PROP_DBUS_DAEMON,

  NUM_PROPERTIES
};

static void
gabble_tls_certificate_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  GabbleTLSCertificate *self = GABBLE_TLS_CERTIFICATE (object);

  switch (property_id)
    {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, self->priv->object_path);
      break;
    case PROP_STATE:
      g_value_set_uint (value, self->priv->cert_state);
      break;
    case PROP_REJECTIONS:
      g_value_set_boxed (value, self->priv->rejections);
      break;
    case PROP_CERTIFICATE_TYPE:
      g_value_set_string (value, self->priv->cert_type);
      break;
    case PROP_CERTIFICATE_CHAIN_DATA:
      g_value_set_boxed (value, self->priv->cert_data);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_tls_certificate_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleTLSCertificate *self = GABBLE_TLS_CERTIFICATE (object);

  switch (property_id)
    {
    case PROP_OBJECT_PATH:
      self->priv->object_path = g_value_dup_string (value);
      break;
    case PROP_CERTIFICATE_TYPE:
      self->priv->cert_type = g_value_dup_string (value);
      break;
    case PROP_CERTIFICATE_CHAIN_DATA:
      self->priv->cert_data = g_value_dup_boxed (value);
      break;
    case PROP_DBUS_DAEMON:
      self->priv->daemon = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, value);
      break;
    }
}

static void
gabble_tls_certificate_finalize (GObject *object)
{
  GabbleTLSCertificate *self = GABBLE_TLS_CERTIFICATE (object);

  tp_clear_boxed (GABBLE_ARRAY_TYPE_TLS_CERTIFICATE_REJECTION_LIST,
      &self->priv->rejections);

  g_free (self->priv->object_path);
  g_free (self->priv->cert_type);
  g_ptr_array_free (self->priv->cert_data, TRUE);

  G_OBJECT_CLASS (gabble_tls_certificate_parent_class)->finalize (object);
}

static void
gabble_tls_certificate_dispose (GObject *object)
{
  GabbleTLSCertificate *self = GABBLE_TLS_CERTIFICATE (object);

  if (self->priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  tp_clear_object (&self->priv->daemon);

  G_OBJECT_CLASS (gabble_tls_certificate_parent_class)->dispose (object);
}

static void
gabble_tls_certificate_constructed (GObject *object)
{
  GabbleTLSCertificate *self = GABBLE_TLS_CERTIFICATE (object);
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (gabble_tls_certificate_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (object);

  /* register the certificate on the bus */
  tp_dbus_daemon_register_object (self->priv->daemon,
      self->priv->object_path, self);
}

static void
gabble_tls_certificate_init (GabbleTLSCertificate *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_TLS_CERTIFICATE, GabbleTLSCertificatePrivate);
  self->priv->rejections = g_ptr_array_new ();
}

static void
gabble_tls_certificate_class_init (GabbleTLSCertificateClass *klass)
{
  static TpDBusPropertiesMixinPropImpl object_props[] = {
    { "State", "state", NULL },
    { "Rejections", "rejections", NULL },
    { "CertificateType", "certificate-type", NULL },
    { "CertificateChainData", "certificate-chain-data", NULL },
    { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
    { GABBLE_IFACE_AUTHENTICATION_TLS_CERTIFICATE,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      object_props,
    },
    { NULL }
  };
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (GabbleTLSCertificatePrivate));

  oclass->finalize = gabble_tls_certificate_finalize;
  oclass->dispose = gabble_tls_certificate_dispose;
  oclass->set_property = gabble_tls_certificate_set_property;
  oclass->get_property = gabble_tls_certificate_get_property;
  oclass->constructed = gabble_tls_certificate_constructed;

  pspec = g_param_spec_string ("object-path",
      "D-Bus object path",
      "The D-Bus object path used for this object on the bus.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_OBJECT_PATH, pspec);

  pspec = g_param_spec_uint ("state",
      "State of this certificate",
      "The state of this TLS certificate.",
      0, NUM_GABBLE_TLS_CERTIFICATE_STATES - 1,
      GABBLE_TLS_CERTIFICATE_STATE_PENDING,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_STATE, pspec);

  pspec = g_param_spec_boxed ("rejections",
      "The reject reasons",
      "The reasons why this TLS certificate has been rejected",
      GABBLE_ARRAY_TYPE_TLS_CERTIFICATE_REJECTION_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_REJECTIONS, pspec);

  pspec = g_param_spec_string ("certificate-type",
      "The certificate type",
      "The type of this certificate.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_CERTIFICATE_TYPE, pspec);

  pspec = g_param_spec_boxed ("certificate-chain-data",
      "The certificate chain data",
      "The raw PEM-encoded trust chain of this certificate.",
      TP_ARRAY_TYPE_UCHAR_ARRAY_LIST,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_CERTIFICATE_CHAIN_DATA, pspec);

  pspec = g_param_spec_object ("dbus-daemon",
      "The DBus daemon connection",
      "The connection to the DBus daemon owning the CM",
      TP_TYPE_DBUS_DAEMON,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_DBUS_DAEMON, pspec);

  klass->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (oclass,
      G_STRUCT_OFFSET (GabbleTLSCertificateClass, dbus_props_class));
}

static void
gabble_tls_certificate_accept (GabbleSvcAuthenticationTLSCertificate *cert,
    DBusGMethodInvocation *context)
{
  GabbleTLSCertificate *self = GABBLE_TLS_CERTIFICATE (cert);

  DEBUG ("Accept() called on the TLS certificate; current state %u",
      self->priv->cert_state);

  if (self->priv->cert_state != GABBLE_TLS_CERTIFICATE_STATE_PENDING)
    {
      GError error =
        { TP_ERRORS,
          TP_ERROR_INVALID_ARGUMENT,
          "Calling Accept() on a certificate with state != PENDING "
          "doesn't make sense."
        };

      dbus_g_method_return_error (context, &error);
      return;
    }

  self->priv->cert_state = GABBLE_TLS_CERTIFICATE_STATE_ACCEPTED;
  gabble_svc_authentication_tls_certificate_emit_accepted (self);

  gabble_svc_authentication_tls_certificate_return_from_accept (context);
}

static void
gabble_tls_certificate_reject (GabbleSvcAuthenticationTLSCertificate *cert,
    const GPtrArray *rejections,
    DBusGMethodInvocation *context)
{
  GabbleTLSCertificate *self = GABBLE_TLS_CERTIFICATE (cert);

  DEBUG ("Reject() called on the TLS certificate with rejections %p, "
      "long %u; current state %u", rejections, rejections->len,
      self->priv->cert_state);

  if (self->priv->cert_state != GABBLE_TLS_CERTIFICATE_STATE_PENDING)
    {
      GError error =
        { TP_ERRORS,
          TP_ERROR_INVALID_ARGUMENT,
          "Calling Reject() on a certificate with state != PENDING "
          "doesn't make sense."
        };

      dbus_g_method_return_error (context, &error);
      return;
    }

  tp_clear_boxed (GABBLE_ARRAY_TYPE_TLS_CERTIFICATE_REJECTION_LIST,
      &self->priv->rejections);

  self->priv->rejections =
    g_boxed_copy (GABBLE_ARRAY_TYPE_TLS_CERTIFICATE_REJECTION_LIST,
        rejections);
  self->priv->cert_state = GABBLE_TLS_CERTIFICATE_STATE_REJECTED;

  gabble_svc_authentication_tls_certificate_emit_rejected (
      self, self->priv->rejections);

  gabble_svc_authentication_tls_certificate_return_from_reject (context);
}

static void
tls_certificate_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  GabbleSvcAuthenticationTLSCertificateClass *klass =
    (GabbleSvcAuthenticationTLSCertificateClass *) (g_iface);

#define IMPLEMENT(x) \
  gabble_svc_authentication_tls_certificate_implement_##x ( \
      klass, gabble_tls_certificate_##x)
  IMPLEMENT (accept);
  IMPLEMENT (reject);
#undef IMPLEMENT
}
