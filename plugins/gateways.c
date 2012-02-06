/* Gateway registration plugin
 *
 * Copyright © 2010 Collabora Ltd. <http://www.collabora.co.uk/>
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

#include "gateways.h"

#include "config.h"

#include <string.h>

#include <telepathy-glib/telepathy-glib.h>

#include <wocky/wocky.h>

#include "extensions/extensions.h"

#include <gabble/gabble.h>

/*************************
 * Plugin implementation *
 *************************/

static guint debug = 0;

#define DEBUG(format, ...) \
G_STMT_START { \
    if (debug != 0) \
      g_debug ("%s: " format, G_STRFUNC, ## __VA_ARGS__); \
} G_STMT_END

static const GDebugKey debug_keys[] = {
      { "gateways", 1 },
      { NULL, 0 }
};

static void plugin_iface_init (
    gpointer g_iface,
    gpointer data);

static const gchar * const sidecar_interfaces[] = {
    GABBLE_IFACE_GABBLE_PLUGIN_GATEWAYS,
    NULL
};

G_DEFINE_TYPE_WITH_CODE (GabbleGatewayPlugin, gabble_gateway_plugin,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_PLUGIN, plugin_iface_init);
    )

static void
gabble_gateway_plugin_init (GabbleGatewayPlugin *self)
{
}

static void
gabble_gateway_plugin_class_init (GabbleGatewayPluginClass *klass)
{
}

static void
gabble_gateway_plugin_create_sidecar_async (
    GabblePlugin *plugin,
    const gchar *sidecar_interface,
    GabbleConnection *connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (plugin),
      callback, user_data,
      gabble_gateway_plugin_create_sidecar_async);
  GabbleSidecar *sidecar = NULL;

  if (!tp_strdiff (sidecar_interface, GABBLE_IFACE_GABBLE_PLUGIN_GATEWAYS))
    {
      sidecar = g_object_new (GABBLE_TYPE_GATEWAY_SIDECAR,
          "connection", connection,
          "session", session,
          NULL);
    }
  else
    {
      g_simple_async_result_set_error (result, TP_ERRORS,
          TP_ERROR_NOT_IMPLEMENTED, "'%s' not implemented", sidecar_interface);
    }

  if (sidecar != NULL)
    g_simple_async_result_set_op_res_gpointer (result, sidecar,
        g_object_unref);

  g_simple_async_result_complete_in_idle (result);
  g_object_unref (result);
}

static GabbleSidecar *
gabble_gateway_plugin_create_sidecar_finish (
    GabblePlugin *plugin,
    GAsyncResult *result,
    GError **error)
{
  GabbleSidecar *sidecar;

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
        error))
    return NULL;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
        G_OBJECT (plugin), gabble_gateway_plugin_create_sidecar_async), NULL);

  sidecar = GABBLE_SIDECAR (g_simple_async_result_get_op_res_gpointer (
        G_SIMPLE_ASYNC_RESULT (result)));

  return g_object_ref (sidecar);
}

static void
plugin_iface_init (
    gpointer g_iface,
    gpointer data G_GNUC_UNUSED)
{
  GabblePluginInterface *iface = g_iface;

  iface->name = "Gateway registration plugin";
  iface->version = PACKAGE_VERSION;
  iface->sidecar_interfaces = sidecar_interfaces;
  iface->create_sidecar_async = gabble_gateway_plugin_create_sidecar_async;
  iface->create_sidecar_finish = gabble_gateway_plugin_create_sidecar_finish;
}

GabblePlugin *
gabble_plugin_create (void)
{
  debug = g_parse_debug_string (g_getenv ("GABBLE_DEBUG"), debug_keys,
      G_N_ELEMENTS (debug_keys) - 1);
  DEBUG ("loaded");

  return g_object_new (GABBLE_TYPE_GATEWAY_PLUGIN,
      NULL);
}

/**************************
 * Sidecar implementation *
 **************************/

enum {
    PROP_0,
    PROP_CONNECTION,
    PROP_SESSION
};

struct _GabbleGatewaySidecarPrivate
{
  WockySession *session;
  TpBaseConnection *connection;
  guint subscribe_id;
  guint subscribed_id;
  GHashTable *gateways;
};

static void sidecar_iface_init (
    gpointer g_iface,
    gpointer data);

static void gateway_iface_init (
    gpointer g_iface,
    gpointer data);

G_DEFINE_TYPE_WITH_CODE (GabbleGatewaySidecar, gabble_gateway_sidecar,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SIDECAR, sidecar_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_GABBLE_PLUGIN_GATEWAYS,
      gateway_iface_init);
    )

static void
gabble_gateway_sidecar_init (GabbleGatewaySidecar *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GABBLE_TYPE_GATEWAY_SIDECAR,
      GabbleGatewaySidecarPrivate);
  self->priv->gateways = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, NULL);
}

static void
gabble_gateway_sidecar_set_property (
    GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleGatewaySidecar *self = GABBLE_GATEWAY_SIDECAR (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_assert (self->priv->connection == NULL);    /* construct-only */
        self->priv->connection = g_value_dup_object (value);
        break;

      case PROP_SESSION:
        g_assert (self->priv->session == NULL);       /* construct-only */
        self->priv->session = g_value_dup_object (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void
gabble_gateway_sidecar_dispose (GObject *object)
{
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (gabble_gateway_sidecar_parent_class)->dispose;
  GabbleGatewaySidecar *self = GABBLE_GATEWAY_SIDECAR (object);

  tp_clear_object (&self->priv->connection);

  if (self->priv->session != NULL)
    {
      WockyPorter *porter = wocky_session_get_porter (self->priv->session);

      wocky_porter_unregister_handler (porter, self->priv->subscribe_id);
      wocky_porter_unregister_handler (porter, self->priv->subscribed_id);
    }

  tp_clear_object (&self->priv->session);

  if (chain_up != NULL)
    chain_up (object);
}

static gboolean
presence_cb (WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  GabbleGatewaySidecar *self = GABBLE_GATEWAY_SIDECAR (user_data);
  const gchar *from;
  gchar *normalized = NULL;
  gboolean ret = FALSE;
  WockyStanzaSubType subtype;

  wocky_stanza_get_type_info (stanza, NULL, &subtype);

  switch (subtype)
    {
    case WOCKY_STANZA_SUB_TYPE_SUBSCRIBED:
      /* Someone has allowed us to subscribe to them */
      break;

    case WOCKY_STANZA_SUB_TYPE_SUBSCRIBE:
      /* Someone wants to subscribe to us */
      break;

    default:
      g_return_val_if_reached (FALSE);
    }

  from = wocky_node_get_attribute (
      wocky_stanza_get_top_node (stanza), "from");

  if (from == NULL || strchr (from, '@') != NULL || strchr (from, '/') != NULL)
    goto finally;

  normalized = wocky_normalise_jid (from);

  if (g_hash_table_lookup (self->priv->gateways, normalized) == NULL)
    goto finally;

  if (subtype == WOCKY_STANZA_SUB_TYPE_SUBSCRIBE)
    {
      WockyStanza *reply;

      /* It's a gateway we've registered with during this session, and they
       * want to subscribe to us. OK, let them. */
      DEBUG ("Allowing gateway '%s' to subscribe to us", normalized);
      reply = wocky_stanza_build (WOCKY_STANZA_TYPE_PRESENCE,
          WOCKY_STANZA_SUB_TYPE_SUBSCRIBED, NULL, normalized,
          NULL);
      wocky_porter_send (porter, reply);
      g_object_unref (reply);
    }
  else
    {
      /* It's a gateway we've registered with during this session, letting us
       * know that yes, we may subscribe to them. Good. */
      DEBUG ("Gateway '%s' allowed us to subscribe to it", normalized);
      /* Eventually, we'll return success from the D-Bus method call here. */
    }

  ret = TRUE;

finally:
  g_free (normalized);
  return ret;
}

static void
gabble_gateway_sidecar_constructed (GObject *object)
{
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (gabble_gateway_sidecar_parent_class)->constructed;
  GabbleGatewaySidecar *self = GABBLE_GATEWAY_SIDECAR (object);
  WockyPorter *porter;

  if (chain_up != NULL)
    chain_up (object);

  g_assert (self->priv->session != NULL);
  g_assert (self->priv->connection != NULL);

  porter = wocky_session_get_porter (self->priv->session);

  self->priv->subscribe_id = wocky_porter_register_handler_from_anyone (porter,
      WOCKY_STANZA_TYPE_PRESENCE, WOCKY_STANZA_SUB_TYPE_SUBSCRIBE,
      WOCKY_PORTER_HANDLER_PRIORITY_MAX, presence_cb, self,
      NULL);
  self->priv->subscribed_id = wocky_porter_register_handler_from_anyone (porter,
      WOCKY_STANZA_TYPE_PRESENCE, WOCKY_STANZA_SUB_TYPE_SUBSCRIBED,
      WOCKY_PORTER_HANDLER_PRIORITY_MAX, presence_cb, self,
      NULL);
}

static void
gabble_gateway_sidecar_class_init (GabbleGatewaySidecarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = gabble_gateway_sidecar_set_property;
  object_class->dispose = gabble_gateway_sidecar_dispose;
  object_class->constructed = gabble_gateway_sidecar_constructed;

  g_type_class_add_private (klass, sizeof (GabbleGatewaySidecarPrivate));

  g_object_class_install_property (object_class, PROP_CONNECTION,
      g_param_spec_object ("connection", "Connection",
          "Gabble connection",
          GABBLE_TYPE_CONNECTION,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_SESSION,
      g_param_spec_object ("session", "Session",
          "Wocky session",
          WOCKY_TYPE_SESSION,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void sidecar_iface_init (
    gpointer g_iface,
    gpointer data)
{
  GabbleSidecarInterface *iface = g_iface;

  iface->interface = GABBLE_IFACE_GABBLE_PLUGIN_GATEWAYS;
  iface->get_immutable_properties = NULL;
}

typedef struct
{
  DBusGMethodInvocation *context;
  gchar *gateway;
} PendingRegistration;

static PendingRegistration *
pending_registration_new (DBusGMethodInvocation *context,
    const gchar *gateway)
{
  PendingRegistration *pr = g_slice_new (PendingRegistration);

  pr->context = context;
  pr->gateway = g_strdup (gateway);
  return pr;
}

static void
pending_registration_free (PendingRegistration *pr)
{
  g_assert (pr->context == NULL);
  g_free (pr->gateway);
  g_slice_free (PendingRegistration, pr);
}

#define NON_NULL (((int *) NULL) + 1)

static void
register_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source);
  PendingRegistration *pr = user_data;
  WockyStanza *reply;
  GError *error = NULL;

  reply = wocky_porter_send_iq_finish (porter, result, &error);

  if (reply == NULL ||
      wocky_stanza_extract_errors (reply, NULL, &error, NULL, NULL))
    {
      GError *tp_error = NULL;

      /* specific error cases for registration: 'conflict' and
       * 'not-acceptable' are documented */
      if (error->domain == WOCKY_XMPP_ERROR)
        {
          switch (error->code)
            {
            case WOCKY_XMPP_ERROR_CONFLICT:
              g_set_error (&tp_error, TP_ERRORS, TP_ERROR_REGISTRATION_EXISTS,
                  "someone else registered that username: %s", error->message);
              break;

            case WOCKY_XMPP_ERROR_NOT_ACCEPTABLE:
              g_set_error (&tp_error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                  "registration not acceptable: %s", error->message);
              break;

            default:
              gabble_set_tp_error_from_wocky (error, &tp_error);
              break;
            }
        }
      else
        {
          /* generic fallback */
          gabble_set_tp_error_from_wocky (error, &tp_error);
        }

      DEBUG ("Failed to register with '%s': %s", pr->gateway,
          tp_error->message);
      dbus_g_method_return_error (pr->context, tp_error);
      pr->context = NULL;
      g_error_free (error);
      g_error_free (tp_error);
    }
  else
    {
      WockyStanza *request;

      DEBUG ("Registered with '%s', exchanging presence...", pr->gateway);

      /* attempt to subscribe to the gateway's presence (FIXME: is this
       * harmless if we're already subscribed to it?) */
      request = wocky_stanza_build (WOCKY_STANZA_TYPE_PRESENCE,
          WOCKY_STANZA_SUB_TYPE_SUBSCRIBE, NULL, pr->gateway,
          NULL);
      wocky_porter_send (porter, request);
      g_object_unref (request);

      gabble_svc_gabble_plugin_gateways_return_from_register (pr->context);
      pr->context = NULL;
    }

  tp_clear_object (&reply);
  pending_registration_free (pr);
}

static void
gateways_register (
    GabbleSvcGabblePluginGateways *sidecar,
    const gchar *gateway,
    const gchar *username,
    const gchar *password,
    DBusGMethodInvocation *context)
{
  GabbleGatewaySidecar *self = GABBLE_GATEWAY_SIDECAR (sidecar);
  WockyPorter *porter = wocky_session_get_porter (self->priv->session);
  WockyStanza *stanza;
  gchar *normalized_gateway;
  GError *error = NULL;

  if (strchr (gateway, '@') != NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Gateway names cannot contain '@': %s", gateway);
      goto error;
    }

  if (strchr (gateway, '/') != NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Gateway names cannot contain '/': %s", gateway);
      goto error;
    }

  if (!wocky_decode_jid (gateway, NULL, &normalized_gateway, NULL))
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Invalid gateway name: %s", gateway);
      goto error;
    }

  DEBUG ("Trying to register on '%s' as '%s'", gateway, username);

  /* steals ownership of normalized_gateway */
  g_hash_table_replace (self->priv->gateways, normalized_gateway, NON_NULL);

  /* This is a *really* minimal implementation. We're meant to ask the gateway
   * what parameters it supports (a XEP-0077 pseudo-form or a XEP-0004 data
   * form), then fill in the blanks to actually make a request.
   *
   * However, because we're hard-coded to take only a username and a password,
   * we might as well just fire off a request with those in and see if it
   * works. If it doesn't, then we couldn't have registered anyway...
   *
   * A more general API for service registration could look like this:
   *
   * method Plugin.GetService() -> o, a{s(*)} [where * is some
   *    representation of the type of the parameter]
   * property Service.Parameters: readable a{s(*)} [likewise]
   * property Service.Registered: readable b
   * method Service.Register(a{sv})
   * method Service.Unregister()
   * method Service.Release() [distributed refcounting]
   */

  stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET,
      NULL, normalized_gateway,
        '(', "query", ':', WOCKY_XEP77_NS_REGISTER,
          '(', "username",
            '$', username,
          ')',
          '(', "password",
            '$', password,
          ')',
        ')',
      NULL);

  wocky_porter_send_iq_async (porter, stanza, NULL, register_cb,
      pending_registration_new (context, normalized_gateway));

  g_object_unref (stanza);
  return;

error:
  DEBUG ("%s", error->message);
  dbus_g_method_return_error (context, error);
  g_error_free (error);
}

static void
gateway_iface_init (
    gpointer klass,
    gpointer data G_GNUC_UNUSED)
{
#define IMPLEMENT(x) gabble_svc_gabble_plugin_gateways_implement_##x (\
    klass, gateways_##x)
  IMPLEMENT (register);
#undef IMPLEMENT
}
