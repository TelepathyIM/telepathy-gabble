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

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/errors.h>

#include <wocky/wocky-namespaces.h>
#include <wocky/wocky-xmpp-error.h>

#include "extensions/extensions.h"

#include <gabble/gabble.h>

/*************************
 * Plugin implementation *
 *************************/

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
gabble_gateway_plugin_create_sidecar (
    GabblePlugin *plugin,
    const gchar *sidecar_interface,
    TpBaseConnection *connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (plugin),
      callback, user_data,
      /* sic: all plugins share gabble_plugin_create_sidecar_finish() so we
       * need to use the same source tag.
       */
      gabble_plugin_create_sidecar);
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

static void
plugin_iface_init (
    gpointer g_iface,
    gpointer data G_GNUC_UNUSED)
{
  GabblePluginInterface *iface = g_iface;

  iface->name = "Gateway registration plugin";
  iface->sidecar_interfaces = sidecar_interfaces;
  iface->create_sidecar = gabble_gateway_plugin_create_sidecar;
}

GabblePlugin *
gabble_plugin_create ()
{
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
  GabbleGatewaySidecar *self = GABBLE_GATEWAY_SIDECAR (object);

  if (self->priv->connection != NULL)
    {
      g_object_unref (self->priv->connection);
      self->priv->connection = NULL;
    }

  if (self->priv->session != NULL)
    {
      g_object_unref (self->priv->session);
      self->priv->session = NULL;
    }
}

static void
gabble_gateway_sidecar_class_init (GabbleGatewaySidecarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = gabble_gateway_sidecar_set_property;
  object_class->dispose = gabble_gateway_sidecar_dispose;

  g_type_class_add_private (klass, sizeof (GabbleGatewaySidecarPrivate));

  g_object_class_install_property (object_class, PROP_CONNECTION,
      g_param_spec_object ("connection", "Connection",
          "Telepathy connection",
          TP_TYPE_BASE_CONNECTION,
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

static void
register_cb (GObject *porter,
    GAsyncResult *result,
    gpointer user_data)
{
  DBusGMethodInvocation *context = user_data;
  WockyXmppStanza *reply;
  GError *error = NULL;

  reply = wocky_porter_send_iq_finish (WOCKY_PORTER (porter), result, &error);

  if (reply == NULL)
    goto finally;

  error = wocky_xmpp_stanza_to_gerror (reply);    /* NULL on success */
  g_object_unref (reply);

finally:
  if (error == NULL)
    {
      gabble_svc_gabble_plugin_gateways_return_from_register (context);
    }
  else
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

      dbus_g_method_return_error (context, tp_error);
      g_error_free (error);
      g_error_free (tp_error);
    }
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
  WockyXmppStanza *stanza;

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

  stanza = wocky_xmpp_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET,
      NULL, gateway,
        WOCKY_NODE, "query", WOCKY_NODE_XMLNS, WOCKY_XEP77_NS_REGISTER,
          WOCKY_NODE, "username",
            WOCKY_NODE_TEXT, username,
          WOCKY_NODE_END,
          WOCKY_NODE, "password",
            WOCKY_NODE_TEXT, password,
          WOCKY_NODE_END,
        WOCKY_NODE_END,
      WOCKY_STANZA_END);

  wocky_porter_send_iq_async (porter, stanza, NULL, register_cb, context);

  g_object_unref (stanza);
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
