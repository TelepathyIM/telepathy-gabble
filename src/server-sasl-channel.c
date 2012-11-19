/*
 * server-sasl-channel.c - Source for GabbleServerSaslChannel
 * Copyright (C) 2010 Collabora Ltd.
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
#include "server-sasl-channel.h"
#include "gabble-signals-marshal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <glib/gstdio.h>
#include <gio/gio.h>
#include <dbus/dbus-glib.h>
#include <wocky/wocky.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#define DEBUG_FLAG GABBLE_DEBUG_AUTH

#include <gabble/error.h>
#include "connection.h"
#include "debug.h"
#include "namespaces.h"
#include "util.h"

static void sasl_auth_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleServerSaslChannel, gabble_server_sasl_channel,
    TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (
        TP_TYPE_SVC_CHANNEL_TYPE_SERVER_AUTHENTICATION,
        NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_SECURABLE, NULL);
    G_IMPLEMENT_INTERFACE (
        TP_TYPE_SVC_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
        sasl_auth_iface_init));

enum
{
  /* server authentication channel */
  PROP_AUTH_METHOD = 1,

  /* sasl authentication channel */
  PROP_AVAILABLE_MECHANISMS,
  PROP_HAS_INITIAL_DATA,
  PROP_CAN_TRY_AGAIN,
  PROP_SECURE,
  PROP_SASL_STATUS,
  PROP_SASL_ERROR,
  PROP_SASL_ERROR_DETAILS,
  PROP_AUTHORIZATION_IDENTITY,
  PROP_DEFAULT_USERNAME,
  PROP_DEFAULT_REALM,

  LAST_PROPERTY,
};

/* private structure */

struct _GabbleServerSaslChannelPrivate
{
  /* Immutable SASL properties */
  GStrv available_mechanisms;
  gboolean secure;

  /* Mutable SASL properties */
  TpSASLStatus sasl_status;
  gchar *sasl_error;
  GHashTable *sasl_error_details;
  /* Given to the Connection on request */
  TpConnectionStatusReason disconnect_reason;
  GError *wocky_auth_error /* = NULL */;

  GSimpleAsyncResult *result;
};

static GPtrArray *
gabble_server_sasl_channel_get_interfaces (TpBaseChannel *base)
{
  GPtrArray *interfaces;

  interfaces = TP_BASE_CHANNEL_CLASS (
      gabble_server_sasl_channel_parent_class)->get_interfaces (base);

  g_ptr_array_add (interfaces, TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION);
  g_ptr_array_add (interfaces, TP_IFACE_CHANNEL_INTERFACE_SECURABLE);

  return interfaces;
}

static void
gabble_server_sasl_channel_init (GabbleServerSaslChannel *self)
{
  GabbleServerSaslChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_SERVER_SASL_CHANNEL, GabbleServerSaslChannelPrivate);

  self->priv = priv;

  priv->sasl_status = TP_SASL_STATUS_NOT_STARTED;
  priv->sasl_error = NULL;
  priv->sasl_error_details = tp_asv_new (NULL, NULL);
  /* a safe assumption if we don't set anything else */
  priv->disconnect_reason = TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED;
}

static void
gabble_server_sasl_channel_fill_immutable_properties (TpBaseChannel *channel,
    GHashTable *properties)
{
  TP_BASE_CHANNEL_CLASS (gabble_server_sasl_channel_parent_class)
    ->fill_immutable_properties (channel, properties);

  tp_dbus_properties_mixin_fill_properties_hash (G_OBJECT (channel),
      properties,
      TP_IFACE_CHANNEL_TYPE_SERVER_AUTHENTICATION, "AuthenticationMethod",
      TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
          "AvailableMechanisms",
      TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION, "HasInitialData",
      TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION, "CanTryAgain",
      TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
          "AuthorizationIdentity",
      TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION, "DefaultRealm",
      TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
          "DefaultUsername",
      TP_IFACE_CHANNEL_INTERFACE_SECURABLE, "Encrypted",
      TP_IFACE_CHANNEL_INTERFACE_SECURABLE, "Verified",
      NULL);
}

static gchar *
gabble_server_sasl_channel_get_object_path_suffix (TpBaseChannel *channel)
{
  return g_strdup ("ServerSASLChannel");
}

static void
gabble_server_sasl_channel_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  TpBaseChannel *channel = TP_BASE_CHANNEL (object);
  GabbleServerSaslChannel *self =
    GABBLE_SERVER_SASL_CHANNEL (object);
  GabbleServerSaslChannelPrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_SASL_STATUS:
      g_value_set_uint (value, priv->sasl_status);
      break;
    case PROP_SASL_ERROR:
      g_value_set_string (value, priv->sasl_error);
      break;
    case PROP_SASL_ERROR_DETAILS:
      g_value_set_boxed (value, priv->sasl_error_details);
      break;
    case PROP_AUTH_METHOD:
      g_value_set_static_string (value,
          TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION);
      break;
    case PROP_AVAILABLE_MECHANISMS:
      g_value_set_boxed (value, priv->available_mechanisms);
      break;
    case PROP_SECURE:
      g_value_set_boolean (value, priv->secure);
      break;
    case PROP_CAN_TRY_AGAIN:
      /* Wocky can't retry SASL authentication (although XMPP can) */
      g_value_set_boolean (value, FALSE);
      break;
    case PROP_HAS_INITIAL_DATA:
      /* Yes, XMPP has "initial data" in its SASL */
      g_value_set_boolean (value, TRUE);
      break;
    case PROP_AUTHORIZATION_IDENTITY:
      /* As per RFC 3920, the authorization identity for c2s connections
       * is the desired JID. We can't use conn_util_get_bare_self_jid at
       * this stage of the connection process, because it hasn't been
       * initialized yet. */
        {
          gchar *jid, *username, *stream_server;

          g_object_get (tp_base_channel_get_connection (channel),
              "username", &username,
              "stream-server", &stream_server,
              NULL);
          jid = g_strconcat (username, "@", stream_server, NULL);
          g_free (username);
          g_free (stream_server);

          g_value_take_string (value, jid);
        }
      break;
    case PROP_DEFAULT_REALM:
      /* Like WockySaslDigestMd5, we use the stream server as the default
       * realm, for interoperability with servers that fail to supply a
       * realm but expect us to have this default. */
      g_object_get_property (
          G_OBJECT (tp_base_channel_get_connection (channel)), "stream-server",
          value);
      break;
    case PROP_DEFAULT_USERNAME:
      /* In practice, XMPP servers normally want us to authenticate as the
       * local-part of the JID. */
      g_object_get_property (
          G_OBJECT (tp_base_channel_get_connection (channel)), "username",
          value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gabble_server_sasl_channel_set_property (GObject *object,
                                   guint property_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
  GabbleServerSaslChannel *chan = GABBLE_SERVER_SASL_CHANNEL (object);
  GabbleServerSaslChannelPrivate *priv = chan->priv;

  switch (property_id)
    {
      case PROP_SECURE:
        priv->secure = g_value_get_boolean (value);
        break;

      case PROP_AVAILABLE_MECHANISMS:
        priv->available_mechanisms = g_value_dup_boxed (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_server_sasl_channel_finalize (GObject *object)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (object);
  GabbleServerSaslChannelPrivate *priv = self->priv;

  /* a ref is held for the channel's lifetime */
  g_assert (tp_base_channel_is_destroyed ((TpBaseChannel *) self));
  g_assert (priv->result == NULL);

  g_strfreev (priv->available_mechanisms);

  g_free (priv->sasl_error);
  g_hash_table_unref (priv->sasl_error_details);
  g_clear_error (&priv->wocky_auth_error);

  if (G_OBJECT_CLASS (gabble_server_sasl_channel_parent_class)->finalize)
    G_OBJECT_CLASS (gabble_server_sasl_channel_parent_class)->finalize (object);
}

static void gabble_server_sasl_channel_close (TpBaseChannel *channel);

static void
gabble_server_sasl_channel_class_init (GabbleServerSaslChannelClass *klass)
{
  static TpDBusPropertiesMixinPropImpl server_auth_props[] = {
    { "AuthenticationMethod", "auth-method", NULL },
    { NULL }
  };
  static TpDBusPropertiesMixinPropImpl sasl_auth_props[] = {
    { "AvailableMechanisms", "available-mechanisms", NULL },
    { "HasInitialData", "has-initial-data", NULL },
    { "CanTryAgain", "can-try-again", NULL },
    { "SASLStatus", "sasl-status", NULL },
    { "SASLError", "sasl-error", NULL },
    { "SASLErrorDetails", "sasl-error-details", NULL },
    { "AuthorizationIdentity", "authorization-identity", NULL },
    { "DefaultRealm", "default-realm", NULL },
    { "DefaultUsername", "default-username", NULL },
    { NULL }
  };
  static TpDBusPropertiesMixinPropImpl securable_props[] = {
    /* For the moment we only have a unified "secure" property, which
     * implies we're both encrypted and verified */
    { "Encrypted", "secure", NULL },
    { "Verified", "secure", NULL },
    { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_CHANNEL_TYPE_SERVER_AUTHENTICATION,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        server_auth_props,
      },
      { TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        sasl_auth_props,
      },
      { TP_IFACE_CHANNEL_INTERFACE_SECURABLE,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        securable_props,
      },
      { NULL }
  };
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  TpBaseChannelClass *channel_class = TP_BASE_CHANNEL_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (GabbleServerSaslChannelPrivate));

  object_class->get_property = gabble_server_sasl_channel_get_property;
  object_class->set_property = gabble_server_sasl_channel_set_property;
  object_class->finalize = gabble_server_sasl_channel_finalize;

  channel_class->channel_type =
    TP_IFACE_CHANNEL_TYPE_SERVER_AUTHENTICATION;
  channel_class->get_interfaces = gabble_server_sasl_channel_get_interfaces;
  channel_class->target_handle_type = TP_HANDLE_TYPE_NONE;
  channel_class->fill_immutable_properties =
    gabble_server_sasl_channel_fill_immutable_properties;
  channel_class->get_object_path_suffix =
    gabble_server_sasl_channel_get_object_path_suffix;
  channel_class->close = gabble_server_sasl_channel_close;

  param_spec = g_param_spec_string ("auth-method",
      "Authentication method",
      "Method of authentication (D-Bus interface)",
      TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_AUTH_METHOD,
      param_spec);

  param_spec = g_param_spec_string ("authorization-identity",
      "AuthorizationIdentity",
      "Identity for which we wish to be authorized",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_AUTHORIZATION_IDENTITY,
      param_spec);

  param_spec = g_param_spec_string ("default-realm",
      "DefaultRealm",
      "Default realm if the server does not supply one",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DEFAULT_REALM,
      param_spec);

  param_spec = g_param_spec_string ("default-username",
      "DefaultUsername",
      "Default simple username if the user does not supply one",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DEFAULT_USERNAME,
      param_spec);

  param_spec = g_param_spec_uint ("sasl-status", "SASLStatus",
      "Status of this channel",
      0, NUM_TP_SASL_STATUSES, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SASL_STATUS,
      param_spec);

  param_spec = g_param_spec_string ("sasl-error", "SASLError",
      "D-Bus error name",
      "", G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SASL_ERROR,
      param_spec);

  param_spec = g_param_spec_boxed ("sasl-error-details", "SASLErrorDetails",
      "Extra details of a SASL error",
      TP_HASH_TYPE_STRING_VARIANT_MAP,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SASL_ERROR_DETAILS,
      param_spec);

  param_spec = g_param_spec_boxed ("available-mechanisms",
      "Available authentication mechanisms",
      "The set of mechanisms the server advertised.",
      G_TYPE_STRV,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_AVAILABLE_MECHANISMS,
      param_spec);

  param_spec = g_param_spec_boolean ("can-try-again", "CanTryAgain",
      "True if failed SASL can be retried without reconnecting",
      FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CAN_TRY_AGAIN,
      param_spec);

  param_spec = g_param_spec_boolean ("has-initial-data", "HasInitialData",
      "True if SASL has initial data",
      TRUE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_HAS_INITIAL_DATA,
      param_spec);

  param_spec = g_param_spec_boolean ("secure",
      "Is secure",
      "Is this channel secure (encrypted and verified)?",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SECURE,
      param_spec);

  klass->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleServerSaslChannelClass, dbus_props_class));
}

static void
set_errors (
    GabbleServerSaslChannel *self,
    const gchar *dbus_error,
    const gchar *debug_message,
    const GError *error)
{
  g_free (self->priv->sasl_error);
  self->priv->sasl_error = g_strdup (dbus_error);

  g_hash_table_remove_all (self->priv->sasl_error_details);
  if (debug_message != NULL)
    tp_asv_set_string (self->priv->sasl_error_details, "debug-message",
        debug_message);

  g_clear_error (&self->priv->wocky_auth_error);
  self->priv->wocky_auth_error = g_error_copy (error);
}

static void
change_current_state (GabbleServerSaslChannel *self,
    TpSASLStatus status)
{
  self->priv->sasl_status = status;

  tp_svc_channel_interface_sasl_authentication_emit_sasl_status_changed (
      self, self->priv->sasl_status,
      self->priv->sasl_error,
      self->priv->sasl_error_details);
}

static void
complete_operation (
    GabbleServerSaslChannel *self,
    gboolean in_idle)
{
  GabbleServerSaslChannelPrivate *priv = self->priv;
  GSimpleAsyncResult *r = priv->result;

  g_return_if_fail (priv->result != NULL);
  priv->result = NULL;

  if (in_idle)
    g_simple_async_result_complete_in_idle (r);
  else
    g_simple_async_result_complete (r);

  g_object_unref (r);
}

/**
 * SASL Authentication Channel Interface
 */

static void gabble_server_sasl_channel_raise (
    DBusGMethodInvocation *context, TpError code, const gchar *message,
    ...) G_GNUC_PRINTF (3, 4);

static void
gabble_server_sasl_channel_raise (DBusGMethodInvocation *context,
    TpError code,
    const gchar *message,
    ...)
{
  va_list ap;
  GError *error = NULL;

  va_start (ap, message);
  error = g_error_new_valist (TP_ERROR, code, message, ap);
  va_end (ap);

  dbus_g_method_return_error (context, error);
  g_error_free (error);
}

/* When called from start_mechanism, initial_data can be NULL. When called
 * from D-Bus as StartMechanismWithData, it can't. */
static void
gabble_server_sasl_channel_start_mechanism_with_data (
    TpSvcChannelInterfaceSASLAuthentication *iface,
    const gchar *in_Mechanism,
    const GArray *in_InitialData,
    DBusGMethodInvocation *context)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (iface);
  GabbleServerSaslChannelPrivate *priv = self->priv;
  WockyAuthRegistryStartData *start_data;
  GString *initial_data = NULL;

  if (self->priv->sasl_status != TP_SASL_STATUS_NOT_STARTED)
    {
      gabble_server_sasl_channel_raise (context, TP_ERROR_NOT_AVAILABLE,
          "Mechanisms can only be started in state Not_Started, not %u",
          self->priv->sasl_status);
      DEBUG ("cannot start: state %u != Not_Started", self->priv->sasl_status);
      return;
    }

  /* NotStarted state is entered by creating the channel: the caller must
   * call start_auth_async immediately */
  g_assert (priv->result != NULL);
  g_assert (g_simple_async_result_is_valid (G_ASYNC_RESULT (priv->result),
        G_OBJECT (self), gabble_server_sasl_channel_start_auth_async));

  if (tp_strv_contains ((const gchar * const *) priv->available_mechanisms,
        in_Mechanism))
    {
      if (in_InitialData != NULL)
        {
          /* The initial data might be secret (for PLAIN etc.), and also might
           * not be UTF-8 or even text, so we just output the length */
          DEBUG ("Starting %s authentication with %u bytes of initial data",
              in_Mechanism, in_InitialData->len);
          initial_data = g_string_new_len (in_InitialData->data,
              in_InitialData->len);
        }
      else
        {
          DEBUG ("Starting %s authentication without initial data",
              in_Mechanism);
        }

      change_current_state (self, TP_SASL_STATUS_IN_PROGRESS);
      dbus_g_method_return (context);

      start_data =
        wocky_auth_registry_start_data_new (in_Mechanism, initial_data);

      g_simple_async_result_set_op_res_gpointer (priv->result,
          start_data, (GDestroyNotify) wocky_auth_registry_start_data_free);
      complete_operation (self, TRUE);

      if (initial_data != NULL)
        g_string_free (initial_data, TRUE);
    }
  else
    {
      DEBUG ("cannot start: %s is not a supported mechanism", in_Mechanism);
      gabble_server_sasl_channel_raise (context, TP_ERROR_NOT_IMPLEMENTED,
          "Selected mechanism is not available.");
    }
}

static void
gabble_server_sasl_channel_start_mechanism (
    TpSvcChannelInterfaceSASLAuthentication *iface,
    const gchar *mech,
    DBusGMethodInvocation *context)
{
  gabble_server_sasl_channel_start_mechanism_with_data (iface, mech, NULL,
      context);
}

static void
gabble_server_sasl_channel_respond (
    TpSvcChannelInterfaceSASLAuthentication *channel,
    const GArray *in_Response_Data,
    DBusGMethodInvocation *context)
{
  GabbleServerSaslChannel *self =
    GABBLE_SERVER_SASL_CHANNEL (channel);
  GabbleServerSaslChannelPrivate *priv = self->priv;
  GString *response_data;

  if (self->priv->sasl_status != TP_SASL_STATUS_IN_PROGRESS)
    {
      gabble_server_sasl_channel_raise (context, TP_ERROR_NOT_AVAILABLE,
          "You can only respond to challenges in state In_Progress, not %u",
          self->priv->sasl_status);
      DEBUG ("cannot respond: state %u != In_Progress",
          self->priv->sasl_status);
      return;
    }

  if (priv->result == NULL)
    {
      gabble_server_sasl_channel_raise (context, TP_ERROR_NOT_AVAILABLE,
          "You already responded to the most recent challenge");
      DEBUG ("cannot respond: already responded");
      return;
    }

  g_assert (g_simple_async_result_is_valid (G_ASYNC_RESULT (priv->result),
        G_OBJECT (self), gabble_server_sasl_channel_challenge_async));

  /* The response might be secret (for PLAIN etc.), and also might
   * not be UTF-8 or even text, so we just output the length */
  DEBUG ("responding with %u bytes", in_Response_Data->len);

  if (in_Response_Data->len > 0)
    response_data = g_string_new_len (in_Response_Data->data,
        in_Response_Data->len);
  else
    response_data = NULL;

  g_simple_async_result_set_op_res_gpointer (priv->result, response_data,
      (GDestroyNotify) wocky_g_string_free);
  complete_operation (self, TRUE);

  tp_svc_channel_interface_sasl_authentication_return_from_respond (
      context);
}

static void
gabble_server_sasl_channel_accept_sasl (
    TpSvcChannelInterfaceSASLAuthentication *channel,
    DBusGMethodInvocation *context)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (channel);
  GabbleServerSaslChannelPrivate *priv = self->priv;
  const gchar *message = NULL;

  switch (self->priv->sasl_status)
    {
    case TP_SASL_STATUS_NOT_STARTED:
      message = "Authentication has not yet begun (Not_Started)";
      break;

    case TP_SASL_STATUS_IN_PROGRESS:
      /* In this state, the only valid time to call this method is in response
       * to a challenge, to indicate that, actually, that challenge was
       * additional data for a successful authentication. */
      if (priv->result == NULL)
        {
          message = "In_Progress, but you already responded to the last "
            "challenge";
        }
      else
        {
          DEBUG ("client says the last challenge was actually final data "
              "and has accepted it");
          g_assert (g_simple_async_result_is_valid (G_ASYNC_RESULT (priv->result),
                G_OBJECT (self), gabble_server_sasl_channel_challenge_async));
          change_current_state (self, TP_SASL_STATUS_CLIENT_ACCEPTED);
        }
      break;

    case TP_SASL_STATUS_SERVER_SUCCEEDED:
      /* The server has already said yes, and the caller is waiting for
       * success_async(), i.e. waiting for the UI to check whether it's
       * happy too. AcceptSASL means that it is. */
      DEBUG ("client has accepted server's success");
      g_assert (g_simple_async_result_is_valid (G_ASYNC_RESULT (priv->result),
            G_OBJECT (self), gabble_server_sasl_channel_success_async));
      change_current_state (self, TP_SASL_STATUS_SUCCEEDED);
      break;

    case TP_SASL_STATUS_CLIENT_ACCEPTED:
      message = "Client already accepted authentication (Client_Accepted)";
      break;

    case TP_SASL_STATUS_SUCCEEDED:
      message = "Authentication already succeeded (Succeeded)";
      break;

    case TP_SASL_STATUS_SERVER_FAILED:
      message = "Authentication has already failed (Server_Failed)";
      break;

    case TP_SASL_STATUS_CLIENT_FAILED:
      message = "Authentication has already been aborted (Client_Failed)";
      break;

    default:
      g_assert_not_reached ();
    }

  if (message != NULL)
    {
      DEBUG ("cannot accept SASL: %s", message);
      gabble_server_sasl_channel_raise (context, TP_ERROR_NOT_AVAILABLE,
          "%s", message);
      return;
    }

  if (priv->result != NULL)
    {
      /* This is a bit weird - this code is run for two different async
       * results. In the In_Progress case, this code results in
       * success with the GSimpleAsyncResult's op_res left as NULL, which
       * is what Wocky wants for an empty response. In the Server_Succeeded
       * response, the async result is just success or error - we succeed. */

      /* We want want to complete not in an idle because if we do we
       * will hit fd.o#32278. This is safe because we're being called
       * from dbus-glib in the main loop. */
      complete_operation (self, FALSE);
    }

  tp_svc_channel_interface_sasl_authentication_return_from_accept_sasl (
      context);
}

static void
gabble_server_sasl_channel_abort_sasl (
    TpSvcChannelInterfaceSASLAuthentication *channel,
    guint in_Reason,
    const gchar *in_Debug_Message,
    DBusGMethodInvocation *context)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (channel);
  GabbleServerSaslChannelPrivate *priv = self->priv;
  const gchar *dbus_error;

  switch (self->priv->sasl_status)
    {
      case TP_SASL_STATUS_SERVER_FAILED:
      case TP_SASL_STATUS_CLIENT_FAILED:
        DEBUG ("ignoring attempt to abort: we already failed");
        break;

      case TP_SASL_STATUS_SUCCEEDED:
      case TP_SASL_STATUS_CLIENT_ACCEPTED:
        DEBUG ("cannot abort: client already called AcceptSASL");
        gabble_server_sasl_channel_raise (context, TP_ERROR_NOT_AVAILABLE,
            "Authentication has already succeeded - too late to abort");
        return;

      case TP_SASL_STATUS_NOT_STARTED:
      case TP_SASL_STATUS_IN_PROGRESS:
      case TP_SASL_STATUS_SERVER_SUCCEEDED:
      {
        GError *error = NULL;

        switch (in_Reason)
          {
            case TP_SASL_ABORT_REASON_INVALID_CHALLENGE:
              g_set_error (&error, WOCKY_AUTH_ERROR,
                  WOCKY_AUTH_ERROR_INVALID_REPLY,
                  "invalid challenge (%s)", in_Debug_Message);
              dbus_error = TP_ERROR_STR_SERVICE_CONFUSED;
              break;

            case TP_SASL_ABORT_REASON_USER_ABORT:
              g_set_error (&error, WOCKY_AUTH_ERROR,
                  WOCKY_AUTH_ERROR_FAILURE,
                  "user aborted auth (%s)", in_Debug_Message);
              dbus_error = TP_ERROR_STR_CANCELLED;
              break;

            default:
              g_set_error (&error, WOCKY_AUTH_ERROR,
                  WOCKY_AUTH_ERROR_FAILURE,
                  "unknown reason code %u, treating as User_Abort (%s)",
                  in_Reason, in_Debug_Message);
              dbus_error = TP_ERROR_STR_CANCELLED;
              break;
          }

        DEBUG ("%s", error->message);

        set_errors (self, dbus_error, in_Debug_Message, error);
        change_current_state (self, TP_SASL_STATUS_CLIENT_FAILED);

        if (priv->result != NULL)
          {
            /* If Not_Started, we're returning failure from start_auth_async.
             * If In_Progress, we might be returning failure from
             *  challenge_async, if one is outstanding.
             * If Server_Succeeded, we're returning failure from success_async.
             */
            g_simple_async_result_set_from_error (priv->result, error);
            complete_operation (self, TRUE);
          }

        g_error_free (error);
        break;
      }
      default:
        g_assert_not_reached ();
    }

  tp_svc_channel_interface_sasl_authentication_return_from_abort_sasl (
      context);
}

static void
sasl_auth_iface_init (gpointer klass,
    gpointer unused G_GNUC_UNUSED)
{
#define IMPLEMENT(x) \
  tp_svc_channel_interface_sasl_authentication_implement_##x (   \
      klass, gabble_server_sasl_channel_##x)
  IMPLEMENT (start_mechanism);
  IMPLEMENT (start_mechanism_with_data);
  IMPLEMENT (respond);
  IMPLEMENT (accept_sasl);
  IMPLEMENT (abort_sasl);
#undef IMPLEMENT
}

void
gabble_server_sasl_channel_start_auth_async (GabbleServerSaslChannel *self,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleServerSaslChannelPrivate *priv = self->priv;

  g_assert (priv->result == NULL);
  g_assert (priv->sasl_status == TP_SASL_STATUS_NOT_STARTED);
  DEBUG ("Starting authentication");

  priv->result = g_simple_async_result_new (G_OBJECT (self), callback,
      user_data, gabble_server_sasl_channel_start_auth_async);
  tp_base_channel_register (TP_BASE_CHANNEL (self));
}

gboolean
gabble_server_sasl_channel_start_auth_finish (GabbleServerSaslChannel *self,
    GAsyncResult *result,
    WockyAuthRegistryStartData **start_data,
    GError **error)
{
  wocky_implement_finish_copy_pointer (self,
      gabble_server_sasl_channel_start_auth_async,
      wocky_auth_registry_start_data_dup, start_data);
}

void
gabble_server_sasl_channel_challenge_async (GabbleServerSaslChannel *self,
    const GString *challenge_data,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleServerSaslChannelPrivate *priv = self->priv;
  GArray *challenge_ay;

  g_assert (!tp_base_channel_is_destroyed ((TpBaseChannel *) self));
  g_assert (priv->result == NULL);

  /* it might be sensitive, and also might not be UTF-8 text, so just print
   * the length */
  DEBUG ("New challenge, %" G_GSIZE_FORMAT " bytes", challenge_data->len);

  priv->result = g_simple_async_result_new (G_OBJECT (self), callback,
      user_data, gabble_server_sasl_channel_challenge_async);

  switch (priv->sasl_status)
    {
      case TP_SASL_STATUS_IN_PROGRESS:
        challenge_ay = g_array_sized_new (FALSE, FALSE, sizeof (gchar),
            challenge_data->len);
        g_array_append_vals (challenge_ay, challenge_data->str,
            challenge_data->len);

        tp_svc_channel_interface_sasl_authentication_emit_new_challenge (
            self, challenge_ay);
        break;
      case TP_SASL_STATUS_CLIENT_FAILED:
        g_return_if_fail (priv->wocky_auth_error != NULL);
        g_simple_async_result_set_from_error (priv->result,
            priv->wocky_auth_error);
        complete_operation (self, TRUE);
        return;
      default:
        g_assert_not_reached ();
    }
}

gboolean
gabble_server_sasl_channel_challenge_finish (GabbleServerSaslChannel *self,
    GAsyncResult *result,
    GString **response,
    GError **error)
{
  wocky_implement_finish_copy_pointer (self,
      gabble_server_sasl_channel_challenge_async,
      wocky_g_string_dup, response);
}

void
gabble_server_sasl_channel_success_async (GabbleServerSaslChannel *self,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleServerSaslChannelPrivate *priv = self->priv;

  g_assert (!tp_base_channel_is_destroyed ((TpBaseChannel *) self));
  g_assert (priv->result == NULL);

  priv->result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data,
      gabble_server_sasl_channel_success_async);

  DEBUG ("");

  if (self->priv->sasl_status != TP_SASL_STATUS_CLIENT_ACCEPTED)
    {
      change_current_state (self, TP_SASL_STATUS_SERVER_SUCCEEDED);
    }
  else
    {
      change_current_state (self, TP_SASL_STATUS_SUCCEEDED);
      complete_operation (self, TRUE);
    }
}

gboolean
gabble_server_sasl_channel_success_finish (GabbleServerSaslChannel *self,
    GAsyncResult *result,
    GError **error)
{
  wocky_implement_finish_void (self,
      gabble_server_sasl_channel_success_async);
}

void
gabble_server_sasl_channel_fail (GabbleServerSaslChannel *self,
    const GError *error)
{
  GError *tp_error = NULL;
  TpConnectionStatusReason conn_reason;

  if (self->priv->sasl_error != NULL)
    {
      DEBUG ("already failed, ignoring further error: %s", error->message);
      return;
    }

  gabble_set_tp_conn_error_from_wocky (error, TP_CONNECTION_STATUS_CONNECTING,
      &conn_reason, &tp_error);
  g_assert (tp_error->domain == TP_ERROR);

  DEBUG ("auth failed: %s", tp_error->message);
  set_errors (self,
      tp_error_get_dbus_name (tp_error->code), tp_error->message, error);
  change_current_state (self, TP_SASL_STATUS_SERVER_FAILED);
  self->priv->disconnect_reason = conn_reason;
}

/*
 * Public
 */

GabbleServerSaslChannel *
gabble_server_sasl_channel_new (GabbleConnection *conn,
    GStrv available_mechanisms,
    gboolean secure,
    const gchar *session_id)
{
  GabbleServerSaslChannel *obj;

  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);

  obj = GABBLE_SERVER_SASL_CHANNEL (
      g_object_new (GABBLE_TYPE_SERVER_SASL_CHANNEL,
        "connection", conn,
        "available-mechanisms", available_mechanisms,
        "secure", secure,
        NULL));

  return obj;
}

static void
gabble_server_sasl_channel_close (TpBaseChannel *channel)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (channel);
  GabbleServerSaslChannelPrivate *priv = self->priv;

  DEBUG ("called on %p", self);

  if (priv->result != NULL)
    {
      DEBUG ("closed channel");

      g_simple_async_result_set_error (priv->result, WOCKY_AUTH_ERROR,
          WOCKY_AUTH_ERROR_FAILURE,
          "%s", "Client aborted authentication.");
      complete_operation (self, TRUE);
    }

  tp_base_channel_destroyed (channel);
}

/**
 * @dbus_error: (out) (transfer full): the D-Bus error name
 * @details: (out) (transfer full) (element-type utf8 GObject.Value): the
 *  error details
 * @reason: (out): the reason with which to disconnect
 *
 * Returns: %TRUE if an error was copied; %FALSE leaving the 'out' parameters
 *  untouched if there is no error
 */
gboolean
gabble_server_sasl_channel_get_failure_details (GabbleServerSaslChannel *self,
    gchar **dbus_error,
    GHashTable **details,
    TpConnectionStatusReason *reason)
{
  if (self->priv->sasl_error != NULL)
    {
      if (dbus_error != NULL)
        *dbus_error = g_strdup (self->priv->sasl_error);

      if (details != NULL)
        *details = g_hash_table_ref (self->priv->sasl_error_details);

      if (reason != NULL)
        *reason = self->priv->disconnect_reason;

      return TRUE;
    }
  else
    {
      return FALSE;
    }
}
