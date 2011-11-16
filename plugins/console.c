/* XML console plugin
 *
 * Copyright © 2011 Collabora Ltd. <http://www.collabora.co.uk/>
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

#include "console.h"

#include "config.h"

#include <string.h>

#include <telepathy-glib/telepathy-glib.h>

#include <wocky/wocky-xmpp-error.h>
#include <wocky/wocky-utils.h>
#include <wocky/wocky-xmpp-reader.h>
#include <wocky/wocky-xmpp-writer.h>

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
      { "console", 1 },
      { NULL, 0 }
};

static void plugin_iface_init (
    gpointer g_iface,
    gpointer data);

static const gchar * const sidecar_interfaces[] = {
    GABBLE_IFACE_GABBLE_PLUGIN_CONSOLE,
    NULL
};

G_DEFINE_TYPE_WITH_CODE (GabbleConsolePlugin, gabble_console_plugin,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_PLUGIN, plugin_iface_init);
    )

static void
gabble_console_plugin_init (GabbleConsolePlugin *self)
{
}

static void
gabble_console_plugin_class_init (GabbleConsolePluginClass *klass)
{
}

static void
gabble_console_plugin_create_sidecar (
    GabblePlugin *plugin,
    const gchar *sidecar_interface,
    GabbleConnection *connection,
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

  if (!tp_strdiff (sidecar_interface, GABBLE_IFACE_GABBLE_PLUGIN_CONSOLE))
    {
      sidecar = g_object_new (GABBLE_TYPE_CONSOLE_SIDECAR,
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

  iface->name = "XMPP console";
  iface->version = PACKAGE_VERSION;
  iface->sidecar_interfaces = sidecar_interfaces;
  iface->create_sidecar = gabble_console_plugin_create_sidecar;
}

GabblePlugin *
gabble_plugin_create (void)
{
  debug = g_parse_debug_string (g_getenv ("GABBLE_DEBUG"), debug_keys,
      G_N_ELEMENTS (debug_keys) - 1);
  DEBUG ("loaded");

  return g_object_new (GABBLE_TYPE_CONSOLE_PLUGIN,
      NULL);
}

/**************************
 * Sidecar implementation *
 **************************/

enum {
    PROP_0,
    PROP_CONNECTION,
    PROP_SESSION,
    PROP_SPEW
};

struct _GabbleConsoleSidecarPrivate
{
  WockySession *session;
  TpBaseConnection *connection;
  WockyXmppReader *reader;
  WockyXmppWriter *writer;

  /* %TRUE if we should emit signals when sending or receiving stanzas */
  gboolean spew;
  /* 0 if spew is FALSE; or a WockyPorter handler id for all incoming stanzas
   * if spew is TRUE. */
  guint incoming_handler;
  /* 0 if spew is FALSE; a GLib signal handler id for WockyPorter::sending if
   * spew is TRUE.
   */
  gulong sending_id;
};

static void sidecar_iface_init (
    gpointer g_iface,
    gpointer data);
static void console_iface_init (
    gpointer g_iface,
    gpointer data);
static void gabble_console_sidecar_set_spew (
    GabbleConsoleSidecar *self,
    gboolean spew);

G_DEFINE_TYPE_WITH_CODE (GabbleConsoleSidecar, gabble_console_sidecar,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SIDECAR, sidecar_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_GABBLE_PLUGIN_CONSOLE,
      console_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    )

static void
gabble_console_sidecar_init (GabbleConsoleSidecar *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GABBLE_TYPE_CONSOLE_SIDECAR,
      GabbleConsoleSidecarPrivate);
  self->priv->reader = wocky_xmpp_reader_new_no_stream ();
  self->priv->writer = wocky_xmpp_writer_new_no_stream ();
}

static void
gabble_console_sidecar_get_property (
    GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  GabbleConsoleSidecar *self = GABBLE_CONSOLE_SIDECAR (object);

  switch (property_id)
    {
      case PROP_SPEW:
        g_value_set_boolean (value, self->priv->spew);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void
gabble_console_sidecar_set_property (
    GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleConsoleSidecar *self = GABBLE_CONSOLE_SIDECAR (object);

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

      case PROP_SPEW:
        gabble_console_sidecar_set_spew (self, g_value_get_boolean (value));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void
gabble_console_sidecar_dispose (GObject *object)
{
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (gabble_console_sidecar_parent_class)->dispose;
  GabbleConsoleSidecar *self = GABBLE_CONSOLE_SIDECAR (object);

  gabble_console_sidecar_set_spew (self, FALSE);

  tp_clear_object (&self->priv->connection);
  tp_clear_object (&self->priv->reader);
  tp_clear_object (&self->priv->writer);
  tp_clear_object (&self->priv->session);

  if (chain_up != NULL)
    chain_up (object);
}

static void
gabble_console_sidecar_class_init (GabbleConsoleSidecarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  static TpDBusPropertiesMixinPropImpl console_props[] = {
      { "SpewStanzas", "spew-stanzas", "spew-stanzas" },
      { NULL },
  };
  static TpDBusPropertiesMixinIfaceImpl interfaces[] = {
      { GABBLE_IFACE_GABBLE_PLUGIN_CONSOLE,
        tp_dbus_properties_mixin_getter_gobject_properties,
        tp_dbus_properties_mixin_setter_gobject_properties,
        console_props
      },
      { NULL },
  };

  object_class->get_property = gabble_console_sidecar_get_property;
  object_class->set_property = gabble_console_sidecar_set_property;
  object_class->dispose = gabble_console_sidecar_dispose;

  g_type_class_add_private (klass, sizeof (GabbleConsoleSidecarPrivate));

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

  g_object_class_install_property (object_class, PROP_SPEW,
      g_param_spec_boolean ("spew-stanzas", "SpewStanzas",
          "If %TRUE, someone wants us to spit out a tonne of stanzas",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  klass->props_class.interfaces = interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleConsoleSidecarClass, props_class));
}

static void sidecar_iface_init (
    gpointer g_iface,
    gpointer data)
{
  GabbleSidecarInterface *iface = g_iface;

  iface->interface = GABBLE_IFACE_GABBLE_PLUGIN_CONSOLE;
  iface->get_immutable_properties = NULL;
}

static gboolean
incoming_cb (
    WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  GabbleConsoleSidecar *self = GABBLE_CONSOLE_SIDECAR (user_data);
  const guint8 *body;
  gsize length;

  wocky_xmpp_writer_write_stanza (self->priv->writer, stanza, &body, &length);
  gabble_svc_gabble_plugin_console_emit_stanza_received (self,
      (const gchar *) body);
  return FALSE;
}

static void
sending_cb (
    WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  GabbleConsoleSidecar *self = GABBLE_CONSOLE_SIDECAR (user_data);

  if (stanza != NULL)
    {
      const guint8 *body;
      gsize length;

      wocky_xmpp_writer_write_stanza (self->priv->writer, stanza, &body,
          &length);
      gabble_svc_gabble_plugin_console_emit_stanza_sent (self,
          (const gchar *) body);
    }
}

static void
gabble_console_sidecar_set_spew (
    GabbleConsoleSidecar *self,
    gboolean spew)
{
  GabbleConsoleSidecarPrivate *priv = self->priv;

  if (!spew != !priv->spew)
    {
      WockyPorter *porter = wocky_session_get_porter (self->priv->session);
      const gchar *props[] = { "SpewStanzas", NULL };

      priv->spew = spew;
      tp_dbus_properties_mixin_emit_properties_changed (G_OBJECT (self),
          GABBLE_IFACE_GABBLE_PLUGIN_CONSOLE, props);

      if (spew)
        {
          g_return_if_fail (priv->incoming_handler == 0);
          priv->incoming_handler = wocky_porter_register_handler_from_anyone (
              porter, WOCKY_STANZA_TYPE_NONE, WOCKY_STANZA_SUB_TYPE_NONE,
              WOCKY_PORTER_HANDLER_PRIORITY_MAX, incoming_cb, self, NULL);

          g_return_if_fail (priv->sending_id == 0);
          priv->sending_id = g_signal_connect (porter, "sending",
              (GCallback) sending_cb, self);
        }
      else
        {
          g_return_if_fail (priv->incoming_handler != 0);
          wocky_porter_unregister_handler (porter, priv->incoming_handler);
          priv->incoming_handler = 0;

          g_return_if_fail (priv->sending_id != 0);
          g_signal_handler_disconnect (porter, priv->sending_id);
          priv->sending_id = 0;
        }
    }
}

static void
return_from_send_iq (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleConsoleSidecar *self = GABBLE_CONSOLE_SIDECAR (source);
  DBusGMethodInvocation *context = user_data;
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);
  GError *error = NULL;

  if (g_simple_async_result_propagate_error (simple, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
  else
    {
      WockyStanza *reply = g_simple_async_result_get_op_res_gpointer (simple);
      WockyStanzaSubType sub_type;
      const guint8 *body;
      gsize length;

      wocky_stanza_get_type_info (reply, NULL, &sub_type);
      wocky_xmpp_writer_write_stanza (self->priv->writer, reply, &body, &length);

      /* woop woop */
      gabble_svc_gabble_plugin_console_return_from_send_iq (context,
          sub_type == WOCKY_STANZA_SUB_TYPE_RESULT ? "result" : "error",
          (const gchar *) body);
    }
}

static void
console_iq_reply_cb (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source);
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
  GError *error = NULL;
  WockyStanza *reply = wocky_porter_send_iq_finish (porter, result, &error);

  if (reply != NULL)
    {
      g_simple_async_result_set_op_res_gpointer (simple, reply, g_object_unref);
    }
  else
    {
      g_simple_async_result_set_from_error (simple, error);
      g_error_free (error);
    }

  g_simple_async_result_complete (simple);
  g_object_unref (simple);
}

static gboolean
get_iq_type (const gchar *type_str,
    WockyStanzaSubType *sub_type_out,
    GError **error)
{
  if (!wocky_strdiff (type_str, "get"))
    {
      *sub_type_out = WOCKY_STANZA_SUB_TYPE_GET;
      return TRUE;
    }

  if (!wocky_strdiff (type_str, "set"))
    {
      *sub_type_out = WOCKY_STANZA_SUB_TYPE_SET;
      return TRUE;
    }

  g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
      "Type must be 'get' or 'set', not '%s'", type_str);
  return FALSE;
}

static gboolean
validate_jid (const gchar **to,
    GError **error)
{
  if (tp_str_empty (*to))
    {
      *to = NULL;
      return TRUE;
    }

  if (wocky_decode_jid (*to, NULL, NULL, NULL))
    return TRUE;

  g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
      "'%s' is not a valid (or empty) JID", *to);
  return FALSE;
}

static gboolean
parse_body_fragment (
    GabbleConsoleSidecar *self,
    const gchar *body,
    WockyNodeTree **fragment_out,
    GError **error)
{
  GabbleConsoleSidecarPrivate *priv = self->priv;
  WockyStanza *stanza;

  wocky_xmpp_reader_reset (priv->reader);
  wocky_xmpp_reader_push (priv->reader, (const guint8 *) body, strlen (body));

  *error = wocky_xmpp_reader_get_error (priv->reader);

  if (*error != NULL)
    return FALSE;

  stanza = wocky_xmpp_reader_pop_stanza (priv->reader);

  if (stanza == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Incomplete stanza! Bad person.");
      return FALSE;
    }

  *fragment_out = (WockyNodeTree *) stanza;
  return TRUE;
}

static void
console_send_iq (
    GabbleSvcGabblePluginConsole *sidecar,
    const gchar *type_str,
    const gchar *to,
    const gchar *body,
    DBusGMethodInvocation *context)
{
  GabbleConsoleSidecar *self = GABBLE_CONSOLE_SIDECAR (sidecar);
  WockyPorter *porter = wocky_session_get_porter (self->priv->session);
  WockyStanzaSubType sub_type;
  WockyNodeTree *fragment;
  GError *error = NULL;

  if (get_iq_type (type_str, &sub_type, &error) &&
      validate_jid (&to, &error) &&
      parse_body_fragment (self, body, &fragment, &error))
    {
      GSimpleAsyncResult *simple = g_simple_async_result_new (G_OBJECT (self),
          return_from_send_iq, context, console_send_iq);
      WockyStanza *stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ, sub_type,
          NULL, to, NULL);

      wocky_node_add_node_tree (wocky_stanza_get_top_node (stanza), fragment);
      wocky_porter_send_iq_async (porter, stanza, NULL, console_iq_reply_cb, simple);
      g_object_unref (fragment);
    }
  else
    {
      DEBUG ("%s", error->message);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

static void
console_iface_init (
    gpointer klass,
    gpointer data G_GNUC_UNUSED)
{
#define IMPLEMENT(x) gabble_svc_gabble_plugin_console_implement_##x (\
    klass, console_##x)
  IMPLEMENT (send_iq);
#undef IMPLEMENT
}
