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
#include <unistd.h>

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

static void channel_iface_init (gpointer, gpointer);
static void sasl_auth_iface_init (gpointer, gpointer);

static void gabble_server_sasl_channel_start_auth_async_func (
    WockyAuthRegistry *auth_registry,
    const GSList *mechanisms,
    gboolean allow_plain,
    gboolean is_secure_channel,
    const gchar *username,
    const gchar *password,
    const gchar *server,
    const gchar *session_id,
    GAsyncReadyCallback callback,
    gpointer user_data);

static gboolean gabble_server_sasl_channel_start_auth_finish_func (
    WockyAuthRegistry *self,
    GAsyncResult *result,
    WockyAuthRegistryStartData **start_data,
    GError **error);

static void  gabble_server_sasl_channel_challenge_async_func (
    WockyAuthRegistry *auth_registry,
    const GString *challenge_data,
    GAsyncReadyCallback callback,
    gpointer user_data);

static gboolean gabble_server_sasl_channel_challenge_finish_func (
    WockyAuthRegistry *self,
    GAsyncResult *result,
    GString **response,
    GError **error);

static void gabble_server_sasl_channel_success_async_func (
    WockyAuthRegistry *auth_registry,
    GAsyncReadyCallback callback,
    gpointer user_data);

static gboolean gabble_server_sasl_channel_success_finish_func (
    WockyAuthRegistry *self,
    GAsyncResult *result,
    GError **error);

static void gabble_server_sasl_channel_failure_func (
    WockyAuthRegistry *auth_registry,
    GError *error);

G_DEFINE_TYPE_WITH_CODE (GabbleServerSaslChannel, gabble_server_sasl_channel,
    WOCKY_TYPE_AUTH_REGISTRY,
    G_IMPLEMENT_INTERFACE (
        TP_TYPE_SVC_CHANNEL,
        channel_iface_init);
    G_IMPLEMENT_INTERFACE (
        TP_TYPE_SVC_DBUS_PROPERTIES,
        tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (
        TP_TYPE_CHANNEL_IFACE,
        NULL);
    G_IMPLEMENT_INTERFACE (
        TP_TYPE_EXPORTABLE_CHANNEL,
        NULL);
    G_IMPLEMENT_INTERFACE (
        GABBLE_TYPE_SVC_CHANNEL_TYPE_SERVER_AUTHENTICATION,
        NULL);
    G_IMPLEMENT_INTERFACE (
        GABBLE_TYPE_SVC_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
        sasl_auth_iface_init));

static const gchar *gabble_server_sasl_channel_interfaces[] = {
  GABBLE_IFACE_CHANNEL_TYPE_SERVER_AUTHENTICATION,
  GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
  NULL
};

enum
{
  /* channel iface */
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_INITIATOR_HANDLE,
  PROP_INITIATOR_ID,
  PROP_TARGET_ID,
  PROP_REQUESTED,
  PROP_CONNECTION,
  PROP_INTERFACES,
  PROP_CHANNEL_DESTROYED,
  PROP_CHANNEL_PROPERTIES,

  /* server authentication channel */
  PROP_AUTH_INFO,
  PROP_AUTH_METHOD,

  /* sasl authentication channel */
  PROP_CURRENT_STATE,
  PROP_MECHANISM,
  PROP_CHALLENGE,
  PROP_AVAILABLE_MECHANISMS,
  PROP_SECURE,

  LAST_PROPERTY,
};

/* private structure */

struct _GabbleServerSaslChannelPrivate
{
  gboolean dispose_has_run;

  /* Channel Iface */
  gchar *object_path;
  GabbleConnection *conn;
  gboolean closed;

  /* Server Authentication Iface*/
  GHashTable *auth_info;

  /* SASL Auth Iface */
  GPtrArray *available_mechanisms;
  GValueArray *current_state;
  GArray *challenge;
  gchar *mechanism;
  gboolean secure;

  GSimpleAsyncResult *result;
};

static void
gabble_server_sasl_channel_init (GabbleServerSaslChannel *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_SERVER_SASL_CHANNEL, GabbleServerSaslChannelPrivate);
}

static GObject *
gabble_server_sasl_channel_constructor (GType type,
                                  guint n_props,
                                  GObjectConstructParam *props)
{
  GObject *obj;
  GabbleServerSaslChannel *self;
  GabbleServerSaslChannelPrivate *priv;

  obj = G_OBJECT_CLASS (gabble_server_sasl_channel_parent_class)->
           constructor (type, n_props, props);

  self = GABBLE_SERVER_SASL_CHANNEL (obj);
  priv = self->priv;

  priv->closed = TRUE;

  priv->challenge = g_array_new (FALSE, FALSE, sizeof (gchar));

  priv->available_mechanisms = g_ptr_array_new_with_free_func (
      (GDestroyNotify) g_free);

  priv->current_state = tp_value_array_build (3,
      G_TYPE_UINT, GABBLE_SASL_STATUS_NOT_STARTED,
      G_TYPE_STRING, "",
      G_TYPE_STRING, "",
      G_TYPE_INVALID);

  return obj;
}

static void
gabble_server_sasl_channel_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  GabbleServerSaslChannel *chan =
    GABBLE_SERVER_SASL_CHANNEL (object);
  GabbleServerSaslChannelPrivate *priv = chan->priv;

  switch (property_id)
    {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value,
          GABBLE_IFACE_CHANNEL_TYPE_SERVER_AUTHENTICATION);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_NONE);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, 0);
      break;
    case PROP_TARGET_ID:
      g_value_set_static_string (value, "");
      break;
    case PROP_INITIATOR_HANDLE:
      g_value_set_uint (value, 0);
      break;
    case PROP_INITIATOR_ID:
      g_value_set_static_string (value, "");
      break;
    case PROP_REQUESTED:
      g_value_set_boolean (value, FALSE);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_INTERFACES:
      g_value_set_boxed (value, gabble_server_sasl_channel_interfaces);
      break;
    case PROP_CHANNEL_DESTROYED:
      g_value_set_boolean (value, priv->closed);
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
              GABBLE_IFACE_CHANNEL_TYPE_SERVER_AUTHENTICATION,
              "AuthenticationInformation",
              GABBLE_IFACE_CHANNEL_TYPE_SERVER_AUTHENTICATION,
              "AuthenticationMethod",
              GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
              "AvailableMechanisms",
              GABBLE_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
              "Secure",
              NULL));
      break;
    case PROP_CURRENT_STATE:
      g_value_set_boxed (value, priv->current_state);
      break;
    case PROP_AUTH_INFO:
      g_value_set_boxed (value, priv->auth_info);
      break;
    case PROP_AUTH_METHOD:
      g_value_set_uint (value, GABBLE_AUTHENTICATION_TYPE_SASL);
      break;
    case PROP_MECHANISM:
      g_value_set_string (value, priv->mechanism);
      break;
    case PROP_AVAILABLE_MECHANISMS:
      g_value_set_boxed (value,
          (gchar **) chan->priv->available_mechanisms->pdata);
      break;
    case PROP_SECURE:
      g_value_set_boolean (value, chan->priv->secure);
      break;
    case PROP_CHALLENGE:
      g_value_set_boxed (value, chan->priv->challenge);
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
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_OBJECT_PATH:
      case PROP_CHANNEL_TYPE:
      case PROP_HANDLE_TYPE:
      case PROP_HANDLE:
        /* no-op */
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_server_sasl_channel_dispose (GObject *object)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (object);
  GabbleServerSaslChannelPrivate *priv = self->priv;

  DEBUG ("disposed");

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  gabble_server_sasl_channel_close (self);

  g_free (priv->mechanism);

  g_free (priv->object_path);

  g_ptr_array_free (priv->available_mechanisms, TRUE);

  g_value_array_free (priv->current_state);

  g_array_free (priv->challenge, TRUE);

  if (priv->auth_info != NULL)
    g_hash_table_unref (priv->auth_info);

  if (G_OBJECT_CLASS (gabble_server_sasl_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_server_sasl_channel_parent_class)->dispose (object);
}

static void
gabble_server_sasl_channel_class_init (GabbleServerSaslChannelClass *klass)
{
  static TpDBusPropertiesMixinPropImpl channel_props[] = {
    { "TargetHandleType", "handle-type", NULL },
    { "TargetHandle", "handle", NULL },
    { "TargetID", "target-id", NULL },
    { "ChannelType", "channel-type", NULL },
    { "Interfaces", "interfaces", NULL },
    { "Requested", "requested", NULL },
    { "InitiatorHandle", "initiator-handle", NULL },
    { "InitiatorID", "initiator-id", NULL },
    { NULL }
  };

  static TpDBusPropertiesMixinPropImpl server_auth_props[] = {
    { "AuthenticationInformation", "auth-info", NULL },
    { "AuthenticationMethod", "auth-method", NULL },
    { NULL }
  };

  static TpDBusPropertiesMixinPropImpl sasl_auth_props[] = {
    { "CurrentState", "current-state", NULL },
    { "AvailableMechanisms", "available-mechanisms", NULL },
    { "Secure", "secure", NULL },
    { "CurrentChallenge", "challenge", NULL },
    { NULL }
  };

  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_CHANNEL,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        channel_props,
      },
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
  WockyAuthRegistryClass *auth_reg_class = WOCKY_AUTH_REGISTRY_CLASS (klass);

  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (GabbleServerSaslChannelPrivate));

  object_class->constructor = gabble_server_sasl_channel_constructor;
  object_class->get_property = gabble_server_sasl_channel_get_property;
  object_class->set_property = gabble_server_sasl_channel_set_property;
  object_class->dispose = gabble_server_sasl_channel_dispose;

  auth_reg_class->start_auth_async_func =
    gabble_server_sasl_channel_start_auth_async_func;
  auth_reg_class->start_auth_finish_func =
    gabble_server_sasl_channel_start_auth_finish_func;

  auth_reg_class->challenge_async_func =
    gabble_server_sasl_channel_challenge_async_func;
  auth_reg_class->challenge_finish_func =
    gabble_server_sasl_channel_challenge_finish_func;

  auth_reg_class->success_async_func =
    gabble_server_sasl_channel_success_async_func;
  auth_reg_class->success_finish_func =
    gabble_server_sasl_channel_success_finish_func;

  auth_reg_class->failure_func = gabble_server_sasl_channel_failure_func;
  auth_reg_class->failure_func = gabble_server_sasl_channel_failure_func;

  /* channel iface */
  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");
  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");
  g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED,
      "channel-destroyed");

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this channel.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  param_spec = g_param_spec_string ("target-id", "Target's identifier",
      "The string obtained by inspecting the target handle",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

  param_spec = g_param_spec_boolean ("requested", "Requested?",
      "True if this channel was requested by the local user",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

  param_spec = g_param_spec_uint ("initiator-handle", "Initiator's handle",
      "The contact who initiated the channel",
      0, G_MAXUINT32, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIATOR_HANDLE,
      param_spec);

  param_spec = g_param_spec_string ("initiator-id", "Initiator's bare JID",
      "The string obtained by inspecting the initiator-handle",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIATOR_ID,
      param_spec);

  param_spec = g_param_spec_boxed ("auth-info",
      "Authentication information",
      "Details used for authentication purposes.",
      GABBLE_HASH_TYPE_AUTHDETAILS,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_AUTH_INFO,
      param_spec);

  param_spec = g_param_spec_uint ("auth-method",
      "Authentication method",
      "Method of authentication like SASL or Captcha.",
      0, NUM_GABBLE_AUTHENTICATION_TYPES, GABBLE_AUTHENTICATION_TYPE_SASL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_AUTH_METHOD,
      param_spec);

  param_spec = g_param_spec_boxed ("current-state",
      "Current state",
      "The state of the current SASL authentication.",
      GABBLE_STRUCT_TYPE_SASL_STATE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CURRENT_STATE, param_spec);

  param_spec = g_param_spec_string ("mechanism", "Mechanism",
      "Mechanism used for this negotiation.",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MECHANISM, param_spec);

  param_spec = g_param_spec_boxed ("available-mechanisms",
      "Available authentication mechanisms",
      "The set of mechanisms the server advertised.",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_AVAILABLE_MECHANISMS,
      param_spec);

  param_spec = g_param_spec_boolean ("secure",
      "Is secure",
      "Is this channel secure (encrypted)?",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SECURE,
      param_spec);

  param_spec = g_param_spec_boxed ("challenge",
      "Challenge",
      "The latest challenge",
      DBUS_TYPE_G_UCHAR_ARRAY,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CHALLENGE,
      param_spec);

  klass->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleServerSaslChannelClass, dbus_props_class));
}

static void
change_current_state (GabbleServerSaslChannel *self,
    GabbleSaslStatus status,
    const gchar *dbus_error,
    const gchar *message)
{
  GabbleServerSaslChannelPrivate *priv = self->priv;
  GValue *value;
  gboolean status_changed;

  value = g_value_array_get_nth (priv->current_state, 0);
  status_changed = (status != g_value_get_uint (value));
  g_value_set_uint (value, status);

  if (dbus_error != NULL)
    {
      value = g_value_array_get_nth (priv->current_state, 1);
      g_value_set_string (value, dbus_error);
    }

  if (message != NULL)
    {
      value = g_value_array_get_nth (priv->current_state, 2);
      g_value_set_string (value, message);
    }

  if (status_changed)
    {
      const gchar *current_message;
      const gchar *current_dbus_error;
      GabbleSaslStatus current_status;

      tp_value_array_unpack (priv->current_state, 3, &current_status,
          &current_dbus_error, &current_message);

      gabble_svc_channel_interface_sasl_authentication_emit_state_changed (
          self, current_status, current_dbus_error, current_message);
    }
}

/**
 * Channel Interface
 */

static void
gabble_server_sasl_channel_close_async (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (iface);

  g_assert (GABBLE_IS_SERVER_SASL_CHANNEL (self));

  gabble_server_sasl_channel_close (self);
  tp_svc_channel_return_from_close (context);
}

static void
gabble_server_sasl_channel_get_interfaces_async (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (iface);

  g_assert (GABBLE_IS_SERVER_SASL_CHANNEL (self));

  tp_svc_channel_return_from_get_interfaces (context,
      gabble_server_sasl_channel_interfaces);
}

static void
channel_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, gabble_server_sasl_channel_##x##suffix)
  IMPLEMENT(close,_async);
  IMPLEMENT(get_interfaces,_async);
#undef IMPLEMENT
}

/**
 * Sasl Authentication Channel Interface
 */

static void
gabble_server_sasl_channel_raise_not_available (DBusGMethodInvocation *context,
    const gchar *message)
{
  GError *error = g_error_new_literal (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
      message);

  dbus_g_method_return_error (context, error);

  g_error_free (error);
}

static void
gabble_server_sasl_channel_start_mechanism (
    GabbleSvcChannelInterfaceSaslAuthentication *mechanisms_chooser,
    const gchar *in_Mechanism,
    const GArray *in_InitialData,
    DBusGMethodInvocation *context)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (
      mechanisms_chooser);
  GabbleServerSaslChannelPrivate *priv = self->priv;
  WockyAuthRegistryStartData *start_data;
  GSimpleAsyncResult *r = priv->result;
  GString *initial_data;
  guint i;
  gboolean mechanism_available = FALSE;
  DEBUG ("");

  if (r == NULL || !g_simple_async_result_is_valid (
          G_ASYNC_RESULT (priv->result), G_OBJECT (self),
          gabble_server_sasl_channel_start_auth_async_func))
    {
      gabble_server_sasl_channel_raise_not_available (context,
          "Authentication not in pre-start state.");

      return;
    }


  for (i = 0; i < priv->available_mechanisms->len - 1; i++)
    {
      gchar *mech =
        (gchar *) g_ptr_array_index (priv->available_mechanisms, i);

      if (g_strcmp0 (mech, in_Mechanism) == 0)
        mechanism_available = TRUE;
    }

  if (mechanism_available)
    {
      priv->result = NULL;

      initial_data = g_string_new_len (in_InitialData->data,
          in_InitialData->len);

      start_data =
        wocky_auth_registry_start_data_new (in_Mechanism, initial_data);

      g_simple_async_result_set_op_res_gpointer (r,
          start_data, (GDestroyNotify) wocky_auth_registry_start_data_free);

      dbus_g_method_return (context);

      g_simple_async_result_complete_in_idle (r);
      g_object_unref (r);
      g_string_free (initial_data, TRUE);
    }
  else
    {
      gabble_server_sasl_channel_raise_not_available (context,
          "Selected mechanism is not available.");
    }
}

static void
gabble_server_sasl_channel_respond (
    GabbleSvcChannelInterfaceSaslAuthentication *channel,
    const GArray *in_Response_Data,
    DBusGMethodInvocation *context)
{
  GabbleServerSaslChannel *self =
    GABBLE_SERVER_SASL_CHANNEL (channel);
  GString *response_data;
  GSimpleAsyncResult *r = self->priv->result;

  if (r == NULL || !g_simple_async_result_is_valid (G_ASYNC_RESULT (r),
          G_OBJECT (self), gabble_server_sasl_channel_challenge_async_func))
    {
      gabble_server_sasl_channel_raise_not_available (context,
          "Authentication waiting for response.");

      return;
    }

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
gabble_server_sasl_channel_accept (
    GabbleSvcChannelInterfaceSaslAuthentication *channel,
    DBusGMethodInvocation *context)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (channel);
  GabbleSaslStatus current_status = g_value_get_uint (g_value_array_get_nth (
          self->priv->current_state, 0));
  GSimpleAsyncResult *r = self->priv->result;
  const gchar *message = NULL;


  switch (current_status)
    {
    case GABBLE_SASL_STATUS_NOT_STARTED:
      message = "Authentication has not yet begun.";
      break;

    case GABBLE_SASL_STATUS_IN_PROGRESS:
      change_current_state (self, GABBLE_SASL_STATUS_CLIENT_ACCEPTED, NULL,
          NULL);
      break;

    case GABBLE_SASL_STATUS_SERVER_SUCCEEDED:
      change_current_state (self, GABBLE_SASL_STATUS_SUCCEEDED, NULL, NULL);
      break;

    case GABBLE_SASL_STATUS_CLIENT_ACCEPTED:
      message = "Client already accepted authentication.";
      break;

    case GABBLE_SASL_STATUS_SUCCEEDED:
      message = "Authentication already succeeded.";
      break;

    case GABBLE_SASL_STATUS_SERVER_FAILED:
    case GABBLE_SASL_STATUS_CLIENT_FAILED:
      message = "Authentication has already failed.";
      break;

    default:
      g_assert_not_reached ();
    }

  if (message != NULL)
    {
      gabble_server_sasl_channel_raise_not_available (context, message);

      return;
    }
  else if (r != NULL)
    {
      self->priv->result = NULL;

      g_simple_async_result_complete_in_idle (r);
      g_object_unref (r);
    }

  gabble_svc_channel_interface_sasl_authentication_return_from_accept (
      context);
}

static void
gabble_server_sasl_channel_abort (
    GabbleSvcChannelInterfaceSaslAuthentication *channel,
    guint in_Reason,
    const gchar *in_Debug_Message,
    DBusGMethodInvocation *context)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (channel);
  GSimpleAsyncResult *r = self->priv->result;
  GabbleSaslStatus current_status = g_value_get_uint (g_value_array_get_nth (
          self->priv->current_state, 0));
  guint code;
  const gchar *dbus_error;

  if (current_status == GABBLE_SASL_STATUS_SUCCEEDED ||
      current_status == GABBLE_SASL_STATUS_CLIENT_ACCEPTED)
    {
      gabble_server_sasl_channel_raise_not_available (context,
          "Authentication has already succeeded.");

      return;
    }

  switch (in_Reason)
    {
      case GABBLE_ABORT_REASON_INVALID_CHALLENGE:
        code = WOCKY_AUTH_ERROR_INVALID_REPLY;
        dbus_error = TP_ERROR_STR_AUTHENTICATION_FAILED;
        break;

      case GABBLE_ABORT_REASON_USER_ABORT:
        code = WOCKY_AUTH_ERROR_FAILURE;
        dbus_error = TP_ERROR_STR_CANCELLED;
        break;

      default:
        g_assert_not_reached ();
    }

  if (r != NULL)
    {
      self->priv->result = NULL;

      g_simple_async_result_set_error (r, WOCKY_AUTH_ERROR, code,
          "Authentication aborted: %s", in_Debug_Message);

      g_simple_async_result_complete_in_idle (r);
      g_object_unref (r);
    }

  change_current_state (self, GABBLE_SASL_STATUS_CLIENT_FAILED, dbus_error,
      in_Debug_Message);

  gabble_svc_channel_interface_sasl_authentication_return_from_abort (context);
}

static void
sasl_auth_iface_init (gpointer klass,
    gpointer unused G_GNUC_UNUSED)
{
#define IMPLEMENT(x) \
  gabble_svc_channel_interface_sasl_authentication_implement_##x (   \
      klass, gabble_server_sasl_channel_##x)
  IMPLEMENT (start_mechanism);
  IMPLEMENT (respond);
  IMPLEMENT (accept);
  IMPLEMENT (abort);
#undef IMPLEMENT
}

/**
 * Auth Registry Interface
 */

static void
gabble_server_sasl_channel_start_auth_async_func (
    WockyAuthRegistry *auth_registry,
    const GSList *mechanisms,
    gboolean allow_plain,
    gboolean is_secure_channel,
    const gchar *username,
    const gchar *password,
    const gchar *server,
    const gchar *session_id,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  if (password == NULL || username == NULL)
    {
      GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (
          auth_registry);
      GabbleServerSaslChannelPrivate *priv = self->priv;
      DBusGConnection *bus = tp_get_bus ();
      TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
      const GSList *i;

      DEBUG ("");

      g_assert (priv->result == NULL);
      g_assert (conn->object_path != NULL);

      g_ptr_array_set_size (priv->available_mechanisms, 0);

      if (priv->auth_info != NULL)
        g_hash_table_unref (priv->auth_info);

      priv->auth_info = tp_asv_new (
          "username", G_TYPE_STRING, username,
          "realm", G_TYPE_STRING, server,
          "session-id", G_TYPE_STRING, session_id,
          NULL);

      priv->result = g_simple_async_result_new (G_OBJECT (self), callback,
          user_data, gabble_server_sasl_channel_start_auth_async_func);

      for (i = mechanisms; i != NULL; i = i->next)
        g_ptr_array_add (priv->available_mechanisms, g_strdup (i->data));

      g_ptr_array_add (priv->available_mechanisms, NULL);

      priv->object_path = g_strdup_printf ("%s/SaslChannel",
          conn->object_path);

      priv->secure = is_secure_channel;

      dbus_g_connection_register_g_object (bus, priv->object_path,
          G_OBJECT (self));

      priv->closed = FALSE;

      g_object_notify (G_OBJECT (self), "channel-destroyed");
    }
  else
    {
      WOCKY_AUTH_REGISTRY_CLASS (
          gabble_server_sasl_channel_parent_class)->start_auth_async_func (
              auth_registry, mechanisms, allow_plain, is_secure_channel,
              username, password, server, session_id, callback, user_data);
    }
}

static gboolean
gabble_server_sasl_channel_start_auth_finish_func (WockyAuthRegistry *self,
    GAsyncResult *result,
    WockyAuthRegistryStartData **start_data,
    GError **error)
{
  if (G_IS_SIMPLE_ASYNC_RESULT (result) &&
      g_simple_async_result_get_source_tag ((GSimpleAsyncResult *) result) ==
        gabble_server_sasl_channel_start_auth_async_func)
    {
      wocky_implement_finish_copy_pointer (self,
          gabble_server_sasl_channel_start_auth_async_func,
          wocky_auth_registry_start_data_dup, start_data);
    }
  else
    {
      return WOCKY_AUTH_REGISTRY_CLASS
        (gabble_server_sasl_channel_parent_class)->start_auth_finish_func (
            self, result, start_data, error);
    }
}

static void
gabble_server_sasl_channel_challenge_async_func (
    WockyAuthRegistry *auth_registry,
    const GString *challenge_data,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (auth_registry);
  GabbleServerSaslChannelPrivate *priv = self->priv;

  if (!priv->closed)
    {
      g_assert (priv->result == NULL);

      priv->result = g_simple_async_result_new (G_OBJECT (self), callback,
          user_data, gabble_server_sasl_channel_challenge_async_func);

      g_array_free (priv->challenge, TRUE);

      priv->challenge = g_array_sized_new (FALSE, FALSE, sizeof (gchar),
          challenge_data->len);

      g_array_append_vals (priv->challenge, challenge_data->str,
          challenge_data->len);

      change_current_state (self, GABBLE_SASL_STATUS_IN_PROGRESS, NULL, NULL);

      gabble_svc_channel_interface_sasl_authentication_emit_new_challenge (
          self, priv->challenge);
    }
  else
    {
      WOCKY_AUTH_REGISTRY_CLASS (
          gabble_server_sasl_channel_parent_class)->challenge_async_func (
              auth_registry, challenge_data, callback, user_data);
    }
}

static gboolean
gabble_server_sasl_channel_challenge_finish_func (WockyAuthRegistry *self,
    GAsyncResult *result,
    GString **response,
    GError **error)
{
  if (G_IS_SIMPLE_ASYNC_RESULT (result) &&
      g_simple_async_result_get_source_tag ((GSimpleAsyncResult *) result) ==
        gabble_server_sasl_channel_challenge_async_func)
    {
      wocky_implement_finish_copy_pointer (self,
          gabble_server_sasl_channel_challenge_async_func,
          wocky_g_string_dup, response);
    }
  else
    {
      return WOCKY_AUTH_REGISTRY_CLASS
        (gabble_server_sasl_channel_parent_class)->challenge_finish_func (
            self, result, response, error);
    }
}

static void
gabble_server_sasl_channel_success_async_func (
    WockyAuthRegistry *auth_registry,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (auth_registry);
  GabbleServerSaslChannelPrivate *priv = self->priv;

  if (!priv->closed)
    {
      GabbleSaslStatus current_status = g_value_get_uint (
          g_value_array_get_nth (priv->current_state, 0));
      GSimpleAsyncResult *r = g_simple_async_result_new (G_OBJECT (self),
          callback, user_data,
          gabble_server_sasl_channel_success_async_func);

      DEBUG ("");

      g_assert (priv->result == NULL);

      if (current_status != GABBLE_SASL_STATUS_CLIENT_ACCEPTED)
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
  else
    {
      WOCKY_AUTH_REGISTRY_CLASS (
          gabble_server_sasl_channel_parent_class)->success_async_func (
              auth_registry, callback, user_data);
    }
}

static gboolean gabble_server_sasl_channel_success_finish_func (
    WockyAuthRegistry *self,
    GAsyncResult *result,
    GError **error)
{
  if (G_IS_SIMPLE_ASYNC_RESULT (result) &&
      g_simple_async_result_get_source_tag ((GSimpleAsyncResult *) result) ==
        gabble_server_sasl_channel_success_async_func)
    {
      wocky_implement_finish_void (self,
          gabble_server_sasl_channel_success_async_func);
    }
  else
    {
      return WOCKY_AUTH_REGISTRY_CLASS
        (gabble_server_sasl_channel_parent_class)->success_finish_func (
            self, result, error);
    }
}

static void
gabble_server_sasl_channel_failure_func (WockyAuthRegistry *auth_registry,
    GError *error)
{
  GabbleServerSaslChannel *self = GABBLE_SERVER_SASL_CHANNEL (auth_registry);
  const gchar *dbus_error = TP_ERROR_STR_NETWORK_ERROR;

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
gabble_server_sasl_channel_new (GabbleConnection *conn)
{
  GabbleServerSaslChannel *obj;

  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);

  obj = GABBLE_SERVER_SASL_CHANNEL (
      g_object_new (GABBLE_TYPE_SERVER_SASL_CHANNEL, "connection", conn,
          NULL));

  return obj;
}

void
gabble_server_sasl_channel_close (GabbleServerSaslChannel *self)
{
  GabbleServerSaslChannelPrivate *priv = self->priv;

  g_assert (GABBLE_IS_SERVER_SASL_CHANNEL (self));

  if (priv->closed)
    return;

  priv->closed = TRUE;

  DEBUG ("called on %p", self);

  tp_svc_channel_emit_closed (self);

  g_object_notify (G_OBJECT (self), "channel-destroyed");

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
}

gboolean
gabble_server_sasl_channel_is_open (GabbleServerSaslChannel *self)
{
  return !self->priv->closed;
}
