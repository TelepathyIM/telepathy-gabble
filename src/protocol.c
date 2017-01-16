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

#include "config.h"

#include "protocol.h"

#include <string.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>
#include <dbus/dbus-protocol.h>
#include <dbus/dbus-glib.h>

#include "extensions/extensions.h"

#include "conn-avatars.h"
#include "conn-presence.h"

#include "connection.h"
#include "connection-manager.h"
#include "im-factory.h"
#ifdef ENABLE_VOIP
#include "media-factory.h"
#endif
#include "private-tubes-factory.h"
#include "roomlist-manager.h"
#include "search-manager.h"
#include "util.h"
#include "addressing-util.h"

#define PROTOCOL_NAME "jabber"
#define ICON_NAME "im-" PROTOCOL_NAME
#define VCARD_FIELD_NAME "x-" PROTOCOL_NAME
#define ENGLISH_NAME "Jabber"

static void addressing_iface_init (TpProtocolAddressingInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GabbleJabberProtocol, gabble_jabber_protocol,
    TP_TYPE_BASE_PROTOCOL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_PROTOCOL_ADDRESSING, addressing_iface_init);
    )

struct _GabbleJabberProtocolPrivate
{
  GPtrArray *params;
};

static const gchar *default_socks5_proxies[] = GABBLE_PARAMS_DEFAULT_SOCKS5_PROXIES;

static void
gabble_jabber_protocol_init (GabbleJabberProtocol *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_JABBER_PROTOCOL, GabbleJabberProtocolPrivate);
}

static GPtrArray *
dup_parameters (TpBaseProtocol *protocol)
{
  GabbleJabberProtocol *self = GABBLE_JABBER_PROTOCOL (protocol);

  if (!self->priv->params)
    {
      self->priv->params = g_ptr_array_new_full (25,
          (GDestroyNotify) tp_cm_param_spec_unref);

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("account",
              TP_CONN_MGR_PARAM_FLAG_REQUIRED | TP_CONN_MGR_PARAM_FLAG_REGISTER,
              g_variant_new_string (""),
              /* FIXME: validate the JID according to the RFC */
              tp_cm_param_filter_string_nonempty, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("password",
              TP_CONN_MGR_PARAM_FLAG_REGISTER | TP_CONN_MGR_PARAM_FLAG_SECRET,
              g_variant_new_string (""),
              NULL, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("server",
              0,
              g_variant_new_string (""),
              /* FIXME: validate the server properly */
              tp_cm_param_filter_string_nonempty, NULL, NULL));


      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("resource",
              0,
              g_variant_new_string (""),
              /* FIXME: validate the resource according to the RFC */
              tp_cm_param_filter_string_nonempty, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("priority",
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
              g_variant_new_int16 (0),
              NULL, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("port",
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
              g_variant_new_uint16 (5222),
              tp_cm_param_filter_uint_nonzero, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("old-ssl",
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
              g_variant_new_boolean (FALSE),
              NULL, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("require-encryption",
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
              g_variant_new_boolean (TRUE),
              NULL, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("register",
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
              g_variant_new_boolean (FALSE),
              NULL, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("low-bandwidth",
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
              g_variant_new_boolean (FALSE),
              NULL, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("https-proxy-server",
              0,
              g_variant_new_string (""),
              /* FIXME: validate properly */
              tp_cm_param_filter_string_nonempty, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("https-proxy-port",
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
              g_variant_new_uint16 (GABBLE_PARAMS_DEFAULT_HTTPS_PROXY_PORT),
              tp_cm_param_filter_uint_nonzero, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("fallback-conference-server",
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
              g_variant_new_string (GABBLE_PARAMS_DEFAULT_FALLBACK_CONFERENCE_SERVER),
              /* FIXME: validate properly */
              tp_cm_param_filter_string_nonempty, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("stun-server",
              0,
              g_variant_new_string (""),
              /* FIXME: validate properly */
              tp_cm_param_filter_string_nonempty, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("stun-port",
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
              g_variant_new_uint16 (GABBLE_PARAMS_DEFAULT_STUN_PORT),
              tp_cm_param_filter_uint_nonzero, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("fallback-stun-server",
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
              g_variant_new_string (GABBLE_PARAMS_DEFAULT_FALLBACK_STUN_SERVER),
              /* FIXME: validate properly */
              tp_cm_param_filter_string_nonempty, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("fallback-stun-port",
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
              g_variant_new_uint16 (GABBLE_PARAMS_DEFAULT_STUN_PORT),
              tp_cm_param_filter_uint_nonzero, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("ignore-ssl-errors",
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
              g_variant_new_boolean (FALSE),
              NULL, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("alias",
              0,
              g_variant_new_string (""),
              /* setting a 0-length alias makes no sense */
              tp_cm_param_filter_string_nonempty, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("fallback-socks5-proxies",
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
              g_variant_new_strv (default_socks5_proxies, -1),
              NULL, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("keepalive-interval",
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
              g_variant_new_uint32 (30),
              NULL, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new (TP_PROP_CONNECTION_INTERFACE_CONTACT_LIST1_DOWNLOAD_AT_CONNECTION,
              TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT | TP_CONN_MGR_PARAM_FLAG_DBUS_PROPERTY,
              g_variant_new_boolean (TRUE),
              NULL, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("fallback-servers",
              0,
              g_variant_new_strv (NULL, 0),
              NULL, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("extra-certificate-identities",
              0,
              g_variant_new_strv (NULL, 0),
              NULL, NULL, NULL));

      g_ptr_array_add (self->priv->params,
          tp_cm_param_spec_new ("account-path-suffix",
              0,
              g_variant_new_string (""),
              NULL, NULL, NULL));
    }

  return g_ptr_array_ref (self->priv->params);
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
  MAP ("server", "explicit-server"),
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
  MAP (TP_PROP_CONNECTION_INTERFACE_CONTACT_LIST1_DOWNLOAD_AT_CONNECTION,
       "download-roster-at-connection"),
  SAME ("fallback-servers"),
  SAME ("extra-certificate-identities"),
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
                       "protocol", PROTOCOL_NAME,
                       "password", tp_asv_get_string (params, "password"),
                       "account-path-suffix", tp_asv_get_string (params,
                           "account-path-suffix"),
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

static const TpPresenceStatusSpec *
get_presence_statuses (TpBaseProtocol *self)
{
  return conn_presence_statuses ();
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
#ifdef ENABLE_FILE_TRANSFER
          GABBLE_TYPE_FT_MANAGER,
#endif
          GABBLE_TYPE_IM_FACTORY,
#ifdef ENABLE_VOIP
          GABBLE_TYPE_MEDIA_FACTORY,
#endif
          GABBLE_TYPE_MUC_FACTORY,
          GABBLE_TYPE_ROOMLIST_MANAGER,
          GABBLE_TYPE_SEARCH_MANAGER,
          GABBLE_TYPE_PRIVATE_TUBES_FACTORY,
          G_TYPE_INVALID };

      *channel_managers = g_memdup (types, sizeof(types));
    }

  if (icon_name != NULL)
    {
      *icon_name = g_strdup (ICON_NAME);
    }

  if (vcard_field != NULL)
    {
      *vcard_field = g_strdup (VCARD_FIELD_NAME);
    }

  if (english_name != NULL)
    {
      *english_name = g_strdup (ENGLISH_NAME);
    }
}

static GStrv
dup_authentication_types (TpBaseProtocol *self)
{
  const gchar * const types[] = {
    TP_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION1,
    TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION1,
    NULL };

  return g_strdupv ((GStrv) types);
}

static GStrv
dup_supported_uri_schemes (TpBaseProtocol *self)
{
  return g_strdupv ((gchar **) gabble_get_addressable_uri_schemes ());
}

static GStrv
dup_supported_vcard_fields (TpBaseProtocol *self)
{
  return g_strdupv ((gchar **) gabble_get_addressable_vcard_fields ());
}

static gchar *
addressing_normalize_vcard_address (TpBaseProtocol *self,
    const gchar *vcard_field,
    const gchar *vcard_address,
    GError **error)
{
  gchar *normalized_address = gabble_normalize_vcard_address (vcard_field, vcard_address, error);

  if (normalized_address == NULL)
    {
      /* InvalidHandle makes no sense in Protocol */
      if (error != NULL && g_error_matches (*error, TP_ERROR, TP_ERROR_INVALID_HANDLE))
        {
          (*error)->code = TP_ERROR_INVALID_ARGUMENT;
        }
    }

  return normalized_address;
}

static gchar *
addressing_normalize_contact_uri (TpBaseProtocol *self,
    const gchar *uri,
    GError **error)
{
  gchar *normalized_address = NULL;

  normalized_address = gabble_normalize_contact_uri (uri, error);

  if (normalized_address == NULL)
    {
      /* InvalidHandle makes no sense in Protocol */
      if (error != NULL && g_error_matches (*error, TP_ERROR, TP_ERROR_INVALID_HANDLE))
        {
          (*error)->code = TP_ERROR_INVALID_ARGUMENT;
        }
    }

  return normalized_address;
}

static gboolean
get_avatar_details (TpBaseProtocol *base,
    GStrv *supported_mime_types,
    guint *min_height,
    guint *min_width,
    guint *rec_height,
    guint *rec_width,
    guint *max_height,
    guint *max_width,
    guint *max_bytes)
{
  gabble_connection_dup_avatar_requirements (supported_mime_types, min_height,
      min_width, rec_height, rec_width, max_height, max_width, max_bytes);

  return TRUE;
}

static void
gabble_jabber_protocol_class_init (GabbleJabberProtocolClass *klass)
{
  TpBaseProtocolClass *base_class =
      (TpBaseProtocolClass *) klass;

  base_class->dup_parameters = dup_parameters;
  base_class->new_connection = new_connection;
  base_class->normalize_contact = normalize_contact;
  base_class->identify_account = identify_account;
  base_class->get_connection_details = get_connection_details;
  base_class->get_statuses = get_presence_statuses;
  base_class->dup_authentication_types = dup_authentication_types;
  base_class->get_avatar_details = get_avatar_details;

  g_type_class_add_private (klass, sizeof (GabbleJabberProtocolPrivate));
}

TpBaseProtocol *
gabble_jabber_protocol_new (void)
{
  return g_object_new (GABBLE_TYPE_JABBER_PROTOCOL,
      "name", PROTOCOL_NAME,
      NULL);
}

static void
addressing_iface_init (TpProtocolAddressingInterface *iface)
{
  iface->dup_supported_vcard_fields = dup_supported_vcard_fields;
  iface->dup_supported_uri_schemes = dup_supported_uri_schemes;
  iface->normalize_vcard_address = addressing_normalize_vcard_address;
  iface->normalize_contact_uri = addressing_normalize_contact_uri;
}
