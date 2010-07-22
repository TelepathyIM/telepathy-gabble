/*
 * protocol.c - source for GabbleJabberProtocol
 * Copyright (C) 2007-2010 Collabora Ltd.
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

#include "protocol.h"

#include <telepathy-glib/base-connection-manager.h>

#include "connection.h"
#include "connection-manager.h"
#include "im-factory.h"
#include "media-factory.h"
#include "private-tubes-factory.h"
#include "roomlist-manager.h"
#include "search-manager.h"
#include "util.h"

G_DEFINE_TYPE (GabbleJabberProtocol,
    gabble_jabber_protocol,
    TP_TYPE_BASE_PROTOCOL)

static void
gabble_jabber_protocol_init (GabbleJabberProtocol *self)
{
}

static const TpCMParamSpec *
get_parameters (TpBaseProtocol *self G_GNUC_UNUSED)
{
  const TpCMProtocolSpec *protocol_spec =
      gabble_connection_manager_get_protocols ();

  /* we know there's only one protocol, and it's jabber */
  return protocol_spec[0].parameters;
}

#define MAP(x,y) { x, y }
#define SAME(x) { x, x }

/* This should be in sync with jabber_params from connection-manager.c,
 * and should contain all settable params/props for the connection,
 * except account and password, which are set manually. */
struct ParamMapping {
  const gchar *tp_param;
  const gchar *conn_prop;
} params2props[] = {
  MAP ("server", "connect-server"),
  SAME ("resource"),
  SAME ("priority"),
  SAME ("port"),
  SAME ("old-ssl"),
  SAME ("require-encryption"),
  SAME ("register"),
  SAME ("low-bandwidth"),
  SAME ("https-proxy-server"),
  SAME ("https-proxy-port"),
  SAME ("fallback-conference-server"),
  SAME ("stun-server"),
  SAME ("stun-port"),
  SAME ("fallback-stun-server"),
  SAME ("fallback-stun-port"),
  SAME ("ignore-ssl-errors"),
  SAME ("alias"),
  SAME ("fallback-socks5-proxies"),
  SAME ("keepalive-interval"),
  MAP (GABBLE_PROP_CONNECTION_INTERFACE_GABBLE_DECLOAK_DECLOAK_AUTOMATICALLY,
       "decloak-automatically"),
  SAME (NULL)
};
#undef SAME
#undef MAP

static TpBaseConnection *
new_connection (TpBaseProtocol *protocol,
                GHashTable *params,
                GError **error)
{
  GabbleConnection *conn;
  guint i;

  conn = g_object_new (GABBLE_TYPE_CONNECTION,
                       "protocol", "jabber",
                       "password", tp_asv_get_string (params, "password"),
                       NULL);

  /* split up account into username, stream-server and resource */
  if (!_gabble_connection_set_properties_from_account (conn,
        tp_asv_get_string (params, "account"), error))
    {
      g_object_unref (G_OBJECT (conn));
      return NULL;
    }

  /* fill in the rest of the properties */
  for (i = 0; params2props[i].tp_param != NULL; i++)
    {
      GValue *val = g_hash_table_lookup (params, params2props[i].tp_param);
      if (val != NULL)
        {
          g_object_set_property (G_OBJECT (conn),
            params2props[i].conn_prop, val);
        }
    }

  return TP_BASE_CONNECTION (conn);
}

static gchar *
normalize_contact (TpBaseProtocol *self G_GNUC_UNUSED,
                   const gchar *contact,
                   GError **error)
{
  return gabble_normalize_contact (NULL, contact,
    GUINT_TO_POINTER (GABBLE_JID_GLOBAL), error);
}

static gchar *
identify_account (TpBaseProtocol *self G_GNUC_UNUSED,
    GHashTable *asv,
    GError **error)
{
  const gchar *account = tp_asv_get_string (asv, "account");

  g_assert (account != NULL);
  return g_strdup (account);
}

static GStrv
get_interfaces (TpBaseProtocol *self)
{
  return g_new0 (gchar *, 1);
}

static void
get_connection_details (TpBaseProtocol *self,
    GStrv *connection_interfaces,
    GType **channel_managers,
    gchar **icon_name,
    gchar **english_name,
    gchar **vcard_field)
{
  if (connection_interfaces != NULL)
    {
      *connection_interfaces = g_strdupv (
          (GStrv) gabble_connection_get_implemented_interfaces ());
    }

  if (channel_managers != NULL)
    {
      GType types[] = {
          GABBLE_TYPE_FT_MANAGER,
          GABBLE_TYPE_IM_FACTORY,
          GABBLE_TYPE_MEDIA_FACTORY,
          GABBLE_TYPE_MUC_FACTORY,
          GABBLE_TYPE_OLPC_GADGET_MANAGER,
          GABBLE_TYPE_ROOMLIST_MANAGER,
          GABBLE_TYPE_SEARCH_MANAGER,
          GABBLE_TYPE_PRIVATE_TUBES_FACTORY,
          G_TYPE_INVALID };

      *channel_managers = g_memdup (types, sizeof(types));
    }

  if (icon_name != NULL)
    {
      *icon_name = g_strdup_printf ("im-%s", tp_base_protocol_get_name (self));
    }

  if (vcard_field != NULL)
    {
      *vcard_field = g_strdup_printf ("x-%s", tp_base_protocol_get_name (self));
    }
}

static void
gabble_jabber_protocol_class_init (GabbleJabberProtocolClass *klass)
{
  TpBaseProtocolClass *base_class =
      (TpBaseProtocolClass *) klass;

  base_class->get_parameters = get_parameters;
  base_class->new_connection = new_connection;
  base_class->normalize_contact = normalize_contact;
  base_class->identify_account = identify_account;
  base_class->get_interfaces = get_interfaces;
  base_class->get_connection_details = get_connection_details;
}

GabbleJabberProtocol *
gabble_jabber_protocol_new (void)
{
  return g_object_new (GABBLE_TYPE_JABBER_PROTOCOL,
      "name", PROTOCOL_NAME,
      NULL);
}

