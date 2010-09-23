/*
 * conn-slacker.h - Header for Gabble connection code handling device idleness
 * Copyright (C) 2010 Collabora Ltd.
 * Copyright (C) 2010 Nokia Corporation
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

#include "conn-slacker.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION
#include "debug.h"
#include "namespaces.h"
#include "util.h"

static void
conn_slacker_send_command (
    GabbleConnection *conn,
    const gchar *command)
{
  LmMessage *stanza = lm_message_build_with_sub_type (NULL,
      LM_MESSAGE_TYPE_IQ, LM_MESSAGE_SUB_TYPE_SET,
      '(', "query", "",
        '@', "xmlns", NS_GOOGLE_QUEUE,
        '(', command, "", ')',
      ')',
      NULL);

  _gabble_connection_send_with_reply (conn, stanza, NULL, NULL, NULL, NULL);
  lm_message_unref (stanza);
}


static void
conn_slacker_update_inactivity (
    GabbleConnection *conn,
    gboolean is_inactive)
{
  if (DEBUGGING)
    {
      gchar *jid = gabble_connection_get_full_jid (conn);

      DEBUG ("device became %sactive; %sabling presence queueing for %s",
          is_inactive ? "in" : "",
          is_inactive ? "en" : "dis",
          jid);
      g_free (jid);
    }

  if (is_inactive)
    {
      conn_slacker_send_command (conn, "enable");
    }
  else
    {
      /* It seems that disabling the queue doesn't flush it, so we need to
       * explicitly flush it too.
       */
      conn_slacker_send_command (conn, "disable");
      conn_slacker_send_command (conn, "flush");
    }
}

static void
conn_slacker_inactivity_changed_cb (
    GabbleSlacker *slacker,
    gboolean is_inactive,
    gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  conn_slacker_update_inactivity (conn, is_inactive);
}

void
gabble_connection_slacker_start (GabbleConnection *conn)
{
  GabbleSlacker *s;

  /* We can only cork presence updates on Google Talk. Of course, the Google
   * Talk server doesn't advertise support for google:queue. So let's use the
   * roster again... */
  if (!(conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER))
    return;

  s = conn->slacker = gabble_slacker_new ();

  /* In the unlikely event of having to use an escape slide... */
  if (G_UNLIKELY (s == NULL))
    return;

  conn->slacker_inactivity_changed_id = g_signal_connect (s,
      "inactivity-changed", (GCallback) conn_slacker_inactivity_changed_cb,
      conn);

  /* If we're already inactive, let's cork right away. (I guess the connection
   * flaked out in the user's pocket?) */
  if (gabble_slacker_is_inactive (s))
    conn_slacker_update_inactivity (conn, TRUE);
}

void
gabble_connection_slacker_stop (GabbleConnection *conn)
{
  if (conn->slacker != NULL)
    {
      g_signal_handler_disconnect (conn->slacker,
          conn->slacker_inactivity_changed_id);
      conn->slacker_inactivity_changed_id = 0;
    }

  tp_clear_object (&conn->slacker);
}
