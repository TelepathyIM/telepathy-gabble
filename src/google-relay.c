/*
 * google-relay.c - Support for Google relays for Jingle
 *
 * Copyright (C) 2006-2008 Collabora Ltd.
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
#include "google-relay.h"

#include <string.h>

#ifdef ENABLE_GOOGLE_RELAY
#include <libsoup/soup.h>
#endif

#include <telepathy-glib/util.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#ifdef G_OS_WIN32
#undef ERROR
#endif

#include "debug.h"

#define RELAY_HTTP_TIMEOUT 5

struct _GabbleGoogleRelayResolver {
#ifdef ENABLE_GOOGLE_RELAY
  SoupSession *soup;
#else
  GObject *soup;
#endif
};

typedef struct
{
  GPtrArray *relays;
  guint component;
  guint requests_to_do;
  GabbleJingleFactoryRelaySessionCb callback;
  gpointer user_data;
} RelaySessionData;

static RelaySessionData *
relay_session_data_new (guint requests_to_do,
    GabbleJingleFactoryRelaySessionCb callback,
    gpointer user_data)
{
  RelaySessionData *rsd = g_slice_new0 (RelaySessionData);

  rsd->relays = g_ptr_array_sized_new (requests_to_do);
  g_ptr_array_set_free_func (rsd->relays, (GDestroyNotify) g_hash_table_unref);
  rsd->component = 1;
  rsd->requests_to_do = requests_to_do;
  rsd->callback = callback;
  rsd->user_data = user_data;

  return rsd;
}

/* This is a GSourceFunc */
static gboolean
relay_session_data_call (gpointer p)
{
  RelaySessionData *rsd = p;

  g_assert (rsd->callback != NULL);

  rsd->callback (rsd->relays, rsd->user_data);

  return FALSE;
}

/* This is a GDestroyNotify */
static void
relay_session_data_destroy (gpointer p)
{
  RelaySessionData *rsd = p;

  g_ptr_array_unref (rsd->relays);

  g_slice_free (RelaySessionData, rsd);
}

#ifdef ENABLE_GOOGLE_RELAY

static void
translate_relay_info (GPtrArray *relays,
    const gchar *relay_ip,
    const gchar *username,
    const gchar *password,
    const gchar *static_type,
    const gchar *port_string,
    guint component)
{
  GHashTable *asv;
  guint64 portll;
  guint port;

  if (port_string == NULL)
    {
      DEBUG ("no relay port for %s found", static_type);
      return;
    }

  portll = g_ascii_strtoull (port_string, NULL, 10);

  if (portll == 0 || portll > G_MAXUINT16)
    {
      DEBUG ("failed to parse relay port '%s' for %s", port_string,
          static_type);
      return;
    }
  port = (guint) portll;

  DEBUG ("type=%s ip=%s port=%u username=%s password=%s component=%u",
      static_type, relay_ip, port, username, password, component);
  /* keys are static, values are slice-allocated */
  asv = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  g_hash_table_insert (asv, "ip",
      tp_g_value_slice_new_string (relay_ip));
  g_hash_table_insert (asv, "type",
      tp_g_value_slice_new_static_string (static_type));
  g_hash_table_insert (asv, "port",
      tp_g_value_slice_new_uint (port));
  g_hash_table_insert (asv, "username",
      tp_g_value_slice_new_string (username));
  g_hash_table_insert (asv, "password",
      tp_g_value_slice_new_string (password));
  g_hash_table_insert (asv, "component",
      tp_g_value_slice_new_uint (component));

  g_ptr_array_add (relays, asv);
}

static void
on_http_response (SoupSession *soup,
    SoupMessage *msg,
    gpointer user_data)
{
  RelaySessionData *rsd = user_data;

  if (msg->status_code != 200)
    {
      DEBUG ("Google session creation failed, relaying not used: %d %s",
          msg->status_code, msg->reason_phrase);
    }
  else
    {
      /* parse a=b lines into GHashTable
       * (key, value both borrowed from items of the strv 'lines') */
      GHashTable *map = g_hash_table_new (g_str_hash, g_str_equal);
      gchar **lines;
      guint i;
      const gchar *relay_ip;
      const gchar *relay_udp_port;
      const gchar *relay_tcp_port;
      const gchar *relay_ssltcp_port;
      const gchar *username;
      const gchar *password;
      gchar *escaped_str;

      escaped_str = g_strescape (msg->response_body->data, "\r\n");
      DEBUG ("Response from Google:\n====\n%s\n====", escaped_str);
      g_free (escaped_str);

      lines = g_strsplit (msg->response_body->data, "\n", 0);

      if (lines != NULL)
        {
          for (i = 0; lines[i] != NULL; i++)
            {
              gchar *delim = strchr (lines[i], '=');
              size_t len;

              if (delim == NULL || delim == lines[i])
                {
                  /* ignore empty keys or lines without '=' */
                  continue;
                }

              len = strlen (lines[i]);

              if (lines[i][len - 1] == '\r')
                {
                  lines[i][len - 1] = '\0';
                }

              *delim = '\0';
              g_hash_table_insert (map, lines[i], delim + 1);
            }
        }

      relay_ip = g_hash_table_lookup (map, "relay.ip");
      relay_udp_port = g_hash_table_lookup (map, "relay.udp_port");
      relay_tcp_port = g_hash_table_lookup (map, "relay.tcp_port");
      relay_ssltcp_port = g_hash_table_lookup (map, "relay.ssltcp_port");
      username = g_hash_table_lookup (map, "username");
      password = g_hash_table_lookup (map, "password");

      if (relay_ip == NULL)
        {
          DEBUG ("No relay.ip found");
        }
      else if (username == NULL)
        {
          DEBUG ("No username found");
        }
      else if (password == NULL)
        {
          DEBUG ("No password found");
        }
      else
        {
          translate_relay_info (rsd->relays, relay_ip, username, password,
              "udp", relay_udp_port, rsd->component);
          translate_relay_info (rsd->relays, relay_ip, username, password,
              "tcp", relay_tcp_port, rsd->component);
          translate_relay_info (rsd->relays, relay_ip, username, password,
              "tls", relay_ssltcp_port, rsd->component);
        }

      g_strfreev (lines);
      g_hash_table_unref (map);
    }

  rsd->component++;

  if ((--rsd->requests_to_do) == 0)
    {
      relay_session_data_call (rsd);
      relay_session_data_destroy (rsd);
    }
}

#endif  /* ENABLE_GOOGLE_RELAY */

GabbleGoogleRelayResolver *
gabble_google_relay_resolver_new (void)
{
  GabbleGoogleRelayResolver *resolver =
      g_slice_new0 (GabbleGoogleRelayResolver);

#ifdef ENABLE_GOOGLE_RELAY

  resolver->soup = soup_session_async_new ();

  /* If we don't get answer in a few seconds, relay won't do
   * us much help anyways. */
  g_object_set (resolver->soup, "timeout", RELAY_HTTP_TIMEOUT, NULL);

#endif

  return resolver;
}

void
gabble_google_relay_resolver_destroy (GabbleGoogleRelayResolver *self)
{
  tp_clear_object (&self->soup);

  g_slice_free (GabbleGoogleRelayResolver, self);
}

void
gabble_google_relay_resolver_resolve (GabbleGoogleRelayResolver *self,
    guint components,
    const gchar *server,
    guint16 port,
    const gchar *token,
    GabbleJingleFactoryRelaySessionCb callback,
    gpointer user_data)
{
  RelaySessionData *rsd =
      relay_session_data_new (components, callback, user_data);

#ifdef ENABLE_GOOGLE_RELAY

  gchar *url;
  guint i;

  if (server == NULL)
    {
      DEBUG ("No relay server provided, not creating google relay session");
      g_idle_add_full (G_PRIORITY_DEFAULT, relay_session_data_call, rsd,
          relay_session_data_destroy);
      return;
    }

  if (token == NULL)
    {
      DEBUG ("No relay token provided, not creating google relay session");
      g_idle_add_full (G_PRIORITY_DEFAULT, relay_session_data_call, rsd,
          relay_session_data_destroy);
      return;
    }

  url = g_strdup_printf ("http://%s:%u/create_session", server, (guint) port);

  for (i = 0; i < components; i++)
    {
      SoupMessage *msg = soup_message_new ("GET", url);

      DEBUG ("Trying to create a new relay session on %s", url);

      /* libjingle sets both headers, so shall we */
      soup_message_headers_append (msg->request_headers,
          "X-Talk-Google-Relay-Auth", token);
      soup_message_headers_append (msg->request_headers,
          "X-Google-Relay-Auth", token);

      soup_session_queue_message (self->soup, msg, on_http_response, rsd);
    }

  g_free (url);

#else  /* !ENABLE_GOOGLE_RELAY */

  DEBUG ("Google relay service is not supported");

  g_idle_add_full (G_PRIORITY_DEFAULT, relay_session_data_call, rsd,
      relay_session_data_destroy);

#endif
}
