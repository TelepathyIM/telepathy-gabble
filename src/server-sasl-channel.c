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
#include <wocky/wocky-auth-registry.h>
#include <wocky/wocky-utils.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-generic.h>

#define DEBUG_FLAG GABBLE_DEBUG_AUTH

#include "connection.h"
#include "debug.h"
#include "namespaces.h"
#include "util.h"

static void sasl_auth_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleServerSaslChannel, gabble_server_sasl_channel,
    TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (
        GABBLE_TYPE_SVC_CHANNEL_TYPE_SERVER_AUTHENTICATION,
        NULL);
    G_IMPLEMENT_INTERFACE (
        GABBLE_TYPE_SVC_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
        sasl_auth_iface_init));

static const gchar *gabble_server_sasl_channel_interfaces[] = {
  GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
  NULL
};

enum
{
  PROP_SESSION_ID = 1,

  /* server authentication channel */
  PROP_AUTH_METHOD,

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
  PROP_SASL_CONTEXT,

  LAST_PROPERTY,
};

/* private structure */

struct _GabbleServerSaslChannelPrivate
{
  /* Immutable SASL properties */
  GStrv available_mechanisms;
  gboolean secure;
  GHashTable *sasl_context;

  /* Mutable SASL properties */
  GabbleSASLStatus sasl_status;
  gchar *sasl_error;
  GHashTable *sasl_error_details;

  GSimpleAsyncResult *result;
};

static void
gabble_server_sasl_channel_init (GabbleServerSaslChannel *self)
{
  GabbleServerSaslChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_SERVER_SASL_CHANNEL, GabbleServerSaslChannelPrivate);

  self->priv = priv;

  priv->sasl_context = tp_asv_new (NULL, NULL);

  priv->sasl_status = GABBLE_SASL_STATUS_NOT_STARTED;
  priv->sasl_error = NULL;
  priv->sasl_error_details = tp_asv_new (NULL, NULL);
}

static void
gabble_server_sasl_channel_fill_immutable_properties (TpBaseChannel *channel,
    GHashTable *properties)
{
  TP_BASE_CHANNEL_CLASS (gabble_server_sasl_channel_parent_class)
    ->fill_immutable_properties (channel, properties);

  tp_dbus_properties_mixin_fill_properties_hash (G_OBJECT (channel),
      properties,
      GABBLE_IFACE_CHANNEL_TYPE_SERVER_AUTHENTICATION, "AuthenticationMethod",
      GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
          "AvailableMechanisms",
      GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION, "HasInitialData",
      GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION, "CanTryAgain",
      GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION, "Encrypted",
      GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION, "Verified",
      GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
          "AuthorizationIdentity",
      GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION, "DefaultRealm",
      /* FIXME: GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
          "DefaultUsername", */
      GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION, "SASLContext",
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
    case PROP_SASL_CONTEXT:
      g_value_set_boxed (value, priv->sasl_context);
      break;
    case PROP_AUTH_METHOD:
      g_value_set_static_string (value,
          GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION);
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
    case PROP_SESSION_ID:
      g_value_set_string (value,
          tp_asv_get_string (priv->sasl_context, "jabber-stream-id"));
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

      case PROP_SESSION_ID:
        g_hash_table_insert (priv->sasl_context, "jabber-stream-id",
            tp_g_value_slice_dup (value));
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
  g_hash_table_unref (priv->sasl_context);

  g_free (priv->sasl_error);
  g_hash_table_unref (priv->sasl_error_details);

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
    /* FIXME: { "DefaultUsername", "default-username", NULL }, */
    { "SASLContext", "sasl-context", NULL },
    /* For the moment we only have a unified "secure" property, which
     * implies we're both encrypted and verified */
    { "Encrypted", "secure", NULL },
    { "Verified", "secure", NULL },
    { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { GABBLE_IFACE_CHANNEL_TYPE_SERVER_AUTHENTICATION,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        server_auth_props,
      },
      { GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        sasl_auth_props,
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
    GABBLE_IFACE_CHANNEL_TYPE_SERVER_AUTHENTICATION;
  channel_class->interfaces = gabble_server_sasl_channel_interfaces;
  channel_class->target_handle_type = TP_HANDLE_TYPE_NONE;
  channel_class->fill_immutable_properties =
    gabble_server_sasl_channel_fill_immutable_properties;
  channel_class->get_object_path_suffix =
    gabble_server_sasl_channel_get_object_path_suffix;
  channel_class->close = gabble_server_sasl_channel_close;

  param_spec = g_param_spec_boxed ("sasl-context", "SASLContext",
      "Extra context for doing SASL",
      TP_HASH_TYPE_STRING_VARIANT_MAP,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SASL_CONTEXT,
      param_spec);

  param_spec = g_param_spec_string ("auth-method",
      "Authentication method",
      "Method of authentication (D-Bus interface)",
      GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_AUTH_METHOD,
      param_spec);

  param_spec = g_param_spec_string ("session-id",
      "Session ID",
      "Jabber <stream> id attribute",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SESSION_ID,
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
      /* FIXME: fix the pluralization in the spec */
      0, NUM_GABBLE_SASL_STATUSS, 0,
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
change_current_state (GabbleServerSaslChannel *self,
    GabbleSASLStatus status,
    const gchar *dbus_error,
    const gchar *debug_message)
{
  self->priv->sasl_status = status;

  g_free (self->priv->sasl_error);
  self->priv->sasl_error = g_strdup (dbus_error);

  g_hash_table_remove_all (self->priv->sasl_error_details);
  if (debug_message != NULL)
    tp_asv_set_string (self->priv->sasl_error_details, "debug-message",
        debug_message);

  gabble_svc_channel_interface_sasl_authentication_emit_sasl_status_changed (
      self, self->priv->sasl_status,
      self->priv->sasl_error,
      self->priv->sasl_error_details);
}

/**
 * SASL Authentication Channel Interface
 */

static void gabble_server_sasl_channel_raise_not_available (
    DBusGMethodInvocation *context, const gchar *message,
    ...) G_GNUC_PRINTF (2, 3);

static void
gabble_server_sasl_channel_raise_not_available (DBusGMethodInvocation *context,
    const gchar *message,
    ...)
{
  va_list ap;
  GError *error = NULL;

  va_start (ap, message);
  error = g_error_new_valist (TP_ERRORS, TP_ERROR_NOT_AVAILABLE, message, ap);
  va_end (ap);

  dbus_g_method_return_error (context, error);
  g_error_free (error);
}

/* When called from start_mechanism, initial_data can be NULL. When called
 * from D-Bus as StartMechanismWithData, it can't. */
static void
gabble_server_sasl_channel_start_mechanism_with_data (
    GabbleSvcChannelInterfaceSASLAuthentication *iface,
    const gchar *in_Mechanism,
    const GArray *in_InitialData,
    DBusGMethodInvocation *context)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (iface);
  GabbleServerSaslChannelPrivate *priv = self->priv;
  WockyAuthRegistryStartData *start_data;
  GSimpleAsyncResult *r = priv->result;
  GString *initial_data = NULL;

  if (self->priv->sasl_status != GABBLE_SASL_STATUS_NOT_STARTED)
    {
      gabble_server_sasl_channel_raise_not_available (context,
          "Mechanisms can only be started in state Not_Started, not %u",
          self->priv->sasl_status);
      DEBUG ("cannot start: state %u != Not_Started", self->priv->sasl_status);
      return;
    }

  /* NotStarted state is entered by creating the channel: the caller must
   * call start_auth_async immediately */
  g_assert (r != NULL);
  g_assert (g_simple_async_result_is_valid (G_ASYNC_RESULT (r),
        G_OBJECT (self), gabble_server_sasl_channel_start_auth_async));

  if (tp_strv_contains ((const gchar * const *) priv->available_mechanisms,
        in_Mechanism))
    {
      priv->result = NULL;

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

          if (g_str_has_prefix (in_Mechanism, "X-WOCKY-JABBER-"))
            {
              /* FIXME: wocky-jabber-auth asserts there is an initial response,
               * and will crash otherwise */
              initial_data = g_string_sized_new (0);
            }
        }

      change_current_state (self, GABBLE_SASL_STATUS_IN_PROGRESS, NULL, NULL);
      dbus_g_method_return (context);

      start_data =
        wocky_auth_registry_start_data_new (in_Mechanism, initial_data);

      g_simple_async_result_set_op_res_gpointer (r,
          start_data, (GDestroyNotify) wocky_auth_registry_start_data_free);
      g_simple_async_result_complete_in_idle (r);
      g_object_unref (r);

      if (initial_data != NULL)
        g_string_free (initial_data, TRUE);
    }
  else
    {
      DEBUG ("cannot start: %s is not a supported mechanism", in_Mechanism);
      gabble_server_sasl_channel_raise_not_available (context,
          "Selected mechanism is not available.");
    }
}

static void
gabble_server_sasl_channel_start_mechanism (
    GabbleSvcChannelInterfaceSASLAuthentication *iface,
    const gchar *mech,
    DBusGMethodInvocation *context)
{
  gabble_server_sasl_channel_start_mechanism_with_data (iface, mech, NULL,
      context);
}

static void
gabble_server_sasl_channel_respond (
    GabbleSvcChannelInterfaceSASLAuthentication *channel,
    const GArray *in_Response_Data,
    DBusGMethodInvocation *context)
{
  GabbleServerSaslChannel *self =
    GABBLE_SERVER_SASL_CHANNEL (channel);
  GString *response_data;
  GSimpleAsyncResult *r = self->priv->result;

  if (self->priv->sasl_status != GABBLE_SASL_STATUS_IN_PROGRESS)
    {
      gabble_server_sasl_channel_raise_not_available (context,
          "You can only respond to challenges in state In_Progress, not %u",
          self->priv->sasl_status);
      DEBUG ("cannot respond: state %u != In_Progress",
          self->priv->sasl_status);
      return;
    }

  if (r == NULL)
    {
      gabble_server_sasl_channel_raise_not_available (context,
          "You already responded to the most recent challenge");
      DEBUG ("cannot respond: already responded");
      return;
    }

  g_assert (g_simple_async_result_is_valid (G_ASYNC_RESULT (r),
        G_OBJECT (self), gabble_server_sasl_channel_challenge_async));

  /* The response might be secret (for PLAIN etc.), and also might
   * not be UTF-8 or even text, so we just output the length */
  DEBUG ("responding with %u bytes", in_Response_Data->len);

  self->priv->result = NULL;

  if (in_Response_Data->len > 0)
    response_data = g_string_new_len (in_Response_Data->data,
        in_Response_Data->len);
  else
    response_data = NULL;

  g_simple_async_result_set_op_res_gpointer (r, response_data,
      (GDestroyNotify) wocky_g_string_free);

  g_simple_async_result_complete_in_idle (r);
  g_object_unref (r);

  gabble_svc_channel_interface_sasl_authentication_return_from_respond (
      context);
}

static void
gabble_server_sasl_channel_accept_sasl (
    GabbleSvcChannelInterfaceSASLAuthentication *channel,
    DBusGMethodInvocation *context)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (channel);
  GSimpleAsyncResult *r = self->priv->result;
  const gchar *message = NULL;


  switch (self->priv->sasl_status)
    {
    case GABBLE_SASL_STATUS_NOT_STARTED:
      message = "Authentication has not yet begun (Not_Started)";
      break;

    case GABBLE_SASL_STATUS_IN_PROGRESS:
      /* In this state, the only valid time to call this method is in response
       * to a challenge, to indicate that, actually, that challenge was
       * additional data for a successful authentication. */
      if (r == NULL)
        {
          message = "In_Progress, but you already responded to the last "
            "challenge";
        }
      else
        {
          DEBUG ("client says the last challenge was actually final data "
              "and has accepted it");
          g_assert (g_simple_async_result_is_valid (G_ASYNC_RESULT (r),
                G_OBJECT (self), gabble_server_sasl_channel_challenge_async));
          change_current_state (self, GABBLE_SASL_STATUS_CLIENT_ACCEPTED, NULL,
              NULL);
        }
      break;

    case GABBLE_SASL_STATUS_SERVER_SUCCEEDED:
      /* The server has already said yes, and the caller is waiting for
       * success_async(), i.e. waiting for the UI to check whether it's
       * happy too. AcceptSASL means that it is. */
      DEBUG ("client has accepted server's success");
      g_assert (g_simple_async_result_is_valid (G_ASYNC_RESULT (r),
            G_OBJECT (self), gabble_server_sasl_channel_success_async));
      change_current_state (self, GABBLE_SASL_STATUS_SUCCEEDED, NULL, NULL);
      break;

    case GABBLE_SASL_STATUS_CLIENT_ACCEPTED:
      message = "Client already accepted authentication (Client_Accepted)";
      break;

    case GABBLE_SASL_STATUS_SUCCEEDED:
      message = "Authentication already succeeded (Succeeded)";
      break;

    case GABBLE_SASL_STATUS_SERVER_FAILED:
      message = "Authentication has already failed (Server_Failed)";
      break;

    case GABBLE_SASL_STATUS_CLIENT_FAILED:
      message = "Authentication has already been aborted (Client_Failed)";
      break;

    default:
      g_assert_not_reached ();
    }

  if (message != NULL)
    {
      DEBUG ("cannot accept SASL: %s", message);
      gabble_server_sasl_channel_raise_not_available (context, "%s", message);
      return;
    }

  if (r != NULL)
    {
      /* This is a bit weird - this code is run for two different async
       * results. In the In_Progress case, this code results in
       * success with the GSimpleAsyncResult's op_res left as NULL, which
       * is what Wocky wants for an empty response. In the Server_Succeeded
       * response, the async result is just success or error - we succeed. */
      self->priv->result = NULL;
      g_simple_async_result_complete_in_idle (r);
      g_object_unref (r);
    }

  gabble_svc_channel_interface_sasl_authentication_return_from_accept_sasl (
      context);
}

static void
gabble_server_sasl_channel_abort_sasl (
    GabbleSvcChannelInterfaceSASLAuthentication *channel,
    guint in_Reason,
    const gchar *in_Debug_Message,
    DBusGMethodInvocation *context)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (channel);
  GSimpleAsyncResult *r = self->priv->result;
  guint code;
  const gchar *dbus_error;

  switch (self->priv->sasl_status)
    {
      case GABBLE_SASL_STATUS_SERVER_FAILED:
      case GABBLE_SASL_STATUS_CLIENT_FAILED:
        DEBUG ("ignoring attempt to abort: we already failed");
        break;

      case GABBLE_SASL_STATUS_SUCCEEDED:
      case GABBLE_SASL_STATUS_CLIENT_ACCEPTED:
        DEBUG ("cannot abort: client already called AcceptSASL");
        gabble_server_sasl_channel_raise_not_available (context,
            "Authentication has already succeeded - too late to abort");
        return;

      case GABBLE_SASL_STATUS_NOT_STARTED:
      case GABBLE_SASL_STATUS_IN_PROGRESS:
      case GABBLE_SASL_STATUS_SERVER_SUCCEEDED:
        switch (in_Reason)
          {
            case GABBLE_SASL_ABORT_REASON_INVALID_CHALLENGE:
              DEBUG ("invalid challenge (%s)", in_Debug_Message);
              code = WOCKY_AUTH_ERROR_INVALID_REPLY;
              /* FIXME: should be ServiceConfused, when it lands in tp-glib */
              dbus_error = TP_ERROR_STR_AUTHENTICATION_FAILED;
              break;

            case GABBLE_SASL_ABORT_REASON_USER_ABORT:
              DEBUG ("user aborted auth (%s)", in_Debug_Message);
              code = WOCKY_AUTH_ERROR_FAILURE;
              dbus_error = TP_ERROR_STR_CANCELLED;
              break;

            default:
              DEBUG ("unknown reason code %u, treating as User_Abort (%s)",
                  in_Reason, in_Debug_Message);
              code = WOCKY_AUTH_ERROR_FAILURE;
              dbus_error = TP_ERROR_STR_CANCELLED;
              break;
          }

        if (r != NULL)
          {
            self->priv->result = NULL;

            /* If Not_Started, we're returning failure from start_auth_async.
             * If In_Progress, we might be returning failure from
             *  challenge_async, if one is outstanding.
             * If Server_Succeeded, we're returning failure from success_async.
             */

            g_simple_async_result_set_error (r, WOCKY_AUTH_ERROR, code,
                "Authentication aborted: %s", in_Debug_Message);

            g_simple_async_result_complete_in_idle (r);
            g_object_unref (r);
          }

        change_current_state (self, GABBLE_SASL_STATUS_CLIENT_FAILED,
            dbus_error, in_Debug_Message);
        break;

      default:
        g_assert_not_reached ();
    }

  gabble_svc_channel_interface_sasl_authentication_return_from_abort_sasl (
      context);
}

static void
sasl_auth_iface_init (gpointer klass,
    gpointer unused G_GNUC_UNUSED)
{
#define IMPLEMENT(x) \
  gabble_svc_channel_interface_sasl_authentication_implement_##x (   \
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
  g_assert (priv->sasl_status == GABBLE_SASL_STATUS_NOT_STARTED);
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
  g_assert (priv->sasl_status == GABBLE_SASL_STATUS_IN_PROGRESS);
  /* it might be sensitive, and also might not be UTF-8 text, so just print
   * the length */
  DEBUG ("New challenge, %" G_GSIZE_FORMAT " bytes", challenge_data->len);

  priv->result = g_simple_async_result_new (G_OBJECT (self), callback,
      user_data, gabble_server_sasl_channel_challenge_async);

  challenge_ay = g_array_sized_new (FALSE, FALSE, sizeof (gchar),
      challenge_data->len);
  g_array_append_vals (challenge_ay, challenge_data->str,
      challenge_data->len);

  gabble_svc_channel_interface_sasl_authentication_emit_new_challenge (
      self, challenge_ay);
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
  GSimpleAsyncResult *r;

  g_assert (!tp_base_channel_is_destroyed ((TpBaseChannel *) self));
  g_assert (priv->result == NULL);

  r = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data,
      gabble_server_sasl_channel_success_async);

  DEBUG ("");

  if (self->priv->sasl_status != GABBLE_SASL_STATUS_CLIENT_ACCEPTED)
    {
      priv->result = r;
      change_current_state (self, GABBLE_SASL_STATUS_SERVER_SUCCEEDED,
          NULL, NULL);
    }
  else
    {
      change_current_state (self, GABBLE_SASL_STATUS_SUCCEEDED, NULL,
          NULL);
      g_simple_async_result_complete_in_idle (r);
      g_object_unref (r);
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
  const gchar *dbus_error = TP_ERROR_STR_NETWORK_ERROR;

  if (self->priv->sasl_error != NULL)
    {
      DEBUG ("already failed, ignoring further error: %s", error->message);
      return;
    }

  if (error->domain == WOCKY_AUTH_ERROR)
    {
      switch (error->code)
        {
        case WOCKY_AUTH_ERROR_INIT_FAILED:
        case WOCKY_AUTH_ERROR_NOT_SUPPORTED:
        case WOCKY_AUTH_ERROR_NO_SUPPORTED_MECHANISMS:
          dbus_error = TP_ERROR_STR_NOT_AVAILABLE;
          break;
        case WOCKY_AUTH_ERROR_STREAM:
        case WOCKY_AUTH_ERROR_NETWORK:
          dbus_error = TP_ERROR_STR_NETWORK_ERROR;
          break;
        case WOCKY_AUTH_ERROR_RESOURCE_CONFLICT:
          dbus_error = TP_ERROR_STR_ALREADY_CONNECTED;
          break;
        case WOCKY_AUTH_ERROR_CONNRESET:
          dbus_error = TP_ERROR_STR_CONNECTION_LOST;
          break;
        default:
          dbus_error = TP_ERROR_STR_AUTHENTICATION_FAILED;
        }
    }

  DEBUG ("auth failed: %s", error->message);

  change_current_state (self, GABBLE_SASL_STATUS_SERVER_FAILED,
      dbus_error, error->message);
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
        "session-id", session_id,
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
      GSimpleAsyncResult *r = priv->result;

      DEBUG ("closed channel");

      priv->result = NULL;
      g_simple_async_result_set_error (r, WOCKY_AUTH_ERROR,
          WOCKY_AUTH_ERROR_FAILURE,
          "%s", "Client aborted authentication.");
      g_simple_async_result_complete_in_idle (r);
      g_object_unref (r);
    }

  tp_base_channel_destroyed (channel);
}
