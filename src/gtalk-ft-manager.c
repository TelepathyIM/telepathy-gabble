/*
 * gtalk-ft-manager.c - Source for GtalkFtManager
 *
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


#include "gtalk-ft-manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#define DEBUG_FLAG GABBLE_DEBUG_SHARE

#include "debug.h"
#include "jingle-factory.h"
#include "jingle-session.h"
#include "jingle-share.h"
#include "namespaces.h"
#include "util.h"

#include <nice/agent.h>

G_DEFINE_TYPE (GtalkFtManager, gtalk_ft_manager, G_TYPE_OBJECT);

typedef enum
  {
    HTTP_SERVER_IDLE,
    HTTP_SERVER_HEADERS,
    HTTP_SERVER_SEND,
    HTTP_CLIENT_IDLE,
    HTTP_CLIENT_RECEIVE,
    HTTP_CLIENT_HEADERS,
    HTTP_CLIENT_CHUNK_SIZE,
    HTTP_CLIENT_CHUNK_END,
    HTTP_CLIENT_CHUNK_FINAL,
    HTTP_CLIENT_BODY,
  } HttpStatus;


typedef struct
{
  NiceAgent *agent;
  guint stream_id;
  guint component_id;
  gboolean agent_attached;
  GabbleJingleShare *content;
  gint channel_id;
  HttpStatus http_status;
  gboolean is_chunked;
  guint64 content_length;
  gchar *write_buffer;
  guint write_len;
  gchar *read_buffer;
  guint read_len;
  gint manifest_entry;
} JingleChannel;


typedef struct
{
  GabbleFileTransferChannel *channel;
  gboolean usable;
  gboolean active;
} GabbleChannel;

struct _GtalkFtManagerPrivate
{
  gboolean dispose_has_run;

  GList *channels;
  GabbleJingleFactory *jingle_factory;
  GabbleJingleSession *jingle;
  GHashTable *jingle_channels;
  gboolean requested;
  gchar *token;
};

static void free_jingle_channel (gpointer data);
static void nice_data_received_cb (NiceAgent *agent,
    guint stream_id, guint component_id, guint len, gchar *buffer,
    gpointer user_data);

static void
gtalk_ft_manager_init (GtalkFtManager *self)
{
  GtalkFtManagerPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (self, GTALK_TYPE_FT_MANAGER,
         GtalkFtManagerPrivate);
  gchar buf[16];
  guint32 *uint_buf = (guint32 *) buf;
  guint i;


  DEBUG ("gtalk ft manager init called");
  self->priv = priv;

  self->priv->jingle_channels = g_hash_table_new_full (NULL, NULL,
      NULL, free_jingle_channel);

  for (i = 0; i < sizeof(buf); i++)
    buf[i] = g_random_int_range (0, 256);

  self->priv->token = g_strdup_printf ("%x%x%x%x",
      uint_buf[0], uint_buf[1], uint_buf[2], uint_buf[3]);

  /* FIXME: we should start creating a nice agent already and have it start
     the candidate gathering.. but we don't know which channel name to
     assign it to... */

  priv->dispose_has_run = FALSE;
}


static void
gtalk_ft_manager_dispose (GObject *object)
{
  GtalkFtManager *self = GTALK_FT_MANAGER (object);
  GList *i;

  if (self->priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  self->priv->dispose_has_run = TRUE;

  if (self->priv->jingle)
    {
      gabble_jingle_session_terminate (self->priv->jingle,
          TP_CHANNEL_GROUP_CHANGE_REASON_NONE, NULL, NULL);

      /* the terminate could synchronously unref it and set it to NULL */
      if (self->priv->jingle)
        {
          g_object_unref (self->priv->jingle);
          self->priv->jingle = NULL;
        }
    }
  if (self->priv->jingle_channels != NULL)
    {
      g_hash_table_destroy (self->priv->jingle_channels);
      self->priv->jingle_channels = NULL;
    }

  for (i = self->priv->channels; i; i = i->next)
    {
      GabbleChannel *c = i->data;
      g_object_unref (c->channel);
      g_slice_free (GabbleChannel, c);
    }
  g_list_free (self->priv->channels);

  g_free (self->priv->token);

  if (G_OBJECT_CLASS (gtalk_ft_manager_parent_class)->dispose)
    G_OBJECT_CLASS (gtalk_ft_manager_parent_class)->dispose (object);
}


static void
gtalk_ft_manager_class_init (GtalkFtManagerClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  g_type_class_add_private (cls, sizeof (GtalkFtManagerPrivate));

  object_class->dispose = gtalk_ft_manager_dispose;
}


static JingleChannel *
get_jingle_channel (GtalkFtManager *self, NiceAgent *agent)
{
  GHashTableIter iter;
  gpointer key, value;
  JingleChannel *ret = NULL;

  g_hash_table_iter_init (&iter, self->priv->jingle_channels);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      JingleChannel *channel = (JingleChannel *) value;
      if (channel->agent == agent)
        {
          ret = channel;
          break;
        }
    }

  return ret;
}


static void
jingle_session_state_changed_cb (GabbleJingleSession *session,
                                 GParamSpec *arg1,
                                 GtalkFtManager *self)
{
  JingleSessionState state;
  GList *i;

  DEBUG ("called");

  g_object_get (session,
      "state", &state,
      NULL);

  switch (state)
    {
      case JS_STATE_INVALID:
      case JS_STATE_PENDING_CREATED:
        break;
      case JS_STATE_PENDING_INITIATE_SENT:
      case JS_STATE_PENDING_INITIATED:
        /* TODO */
        for (i = self->priv->channels; i; i = i->next)
          {
            GabbleChannel *c = i->data;
            gabble_file_transfer_channel_set_gtalk_ft_state (c->channel,
                PENDING, NONE);
          }
        break;
      case JS_STATE_PENDING_ACCEPT_SENT:
      case JS_STATE_ACTIVE:
        /* Do not set the channels to OPEN unless we're ready to send/receive
           data from them */
        /* TODO */
        for (i = self->priv->channels; i; i = i->next)
          {
            GabbleChannel *c = i->data;
            gabble_file_transfer_channel_set_gtalk_ft_state (c->channel,
                ACCEPTED, NONE);
          }
        break;
      case JS_STATE_ENDED:
        /* Do nothing, let the terminated signal set the correct state
           depending on the termination reason */
      default:
        break;
    }
}

static void
jingle_session_terminated_cb (GabbleJingleSession *session,
                       gboolean local_terminator,
                       TpChannelGroupChangeReason reason,
                       const gchar *text,
                       gpointer user_data)
{
  GtalkFtManager *self = GTALK_FT_MANAGER (user_data);
  GList *i;

  g_assert (session == self->priv->jingle);

  for (i = self->priv->channels; i; i = i->next)
    {
      GabbleChannel *c = i->data;
      gabble_file_transfer_channel_set_gtalk_ft_state (c->channel, TERMINATED,
          local_terminator ? LOCAL_STOPPED: REMOTE_STOPPED);
    }
}

static void
content_new_remote_candidates_cb (GabbleJingleContent *content,
    GList *clist, gpointer user_data)
{
  GtalkFtManager *self = GTALK_FT_MANAGER (user_data);
  GList *li;

  DEBUG ("Got new remote candidates");

  for (li = clist; li; li = li->next)
    {
      JingleCandidate *candidate = li->data;
      NiceCandidate *cand = NULL;
      JingleChannel *channel = NULL;
      GSList *candidates = NULL;

      if (candidate->type != JINGLE_TRANSPORT_PROTOCOL_UDP)
        continue;

      channel = g_hash_table_lookup (self->priv->jingle_channels,
          GINT_TO_POINTER (candidate->component));
      if (channel == NULL)
        continue;

      cand = nice_candidate_new (
          candidate->type == JINGLE_CANDIDATE_TYPE_LOCAL?
          NICE_CANDIDATE_TYPE_HOST:
          candidate->type == JINGLE_CANDIDATE_TYPE_STUN?
          NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE:
          NICE_CANDIDATE_TYPE_RELAYED);


      cand->transport = JINGLE_TRANSPORT_PROTOCOL_UDP;
      nice_address_init (&cand->addr);
      nice_address_set_from_string (&cand->addr, candidate->address);
      nice_address_set_port (&cand->addr, candidate->port);
      cand->priority = candidate->preference * 1000;
      cand->stream_id = channel->stream_id;
      cand->component_id = channel->component_id;
      /*
      if (c->id == NULL)
        candidate_id = g_strdup_printf ("R%d", ++priv->remote_candidate_count);
      else
      candidate_id = c->id;*/
      if (candidate->id)
        strncpy (cand->foundation, candidate->id,
            NICE_CANDIDATE_MAX_FOUNDATION - 1);
      cand->username = g_strdup (candidate->username?candidate->username:"");
      cand->password = g_strdup (candidate->password?candidate->password:"");

      candidates = g_slist_append (candidates, cand);
      nice_agent_set_remote_candidates (channel->agent,
          channel->stream_id, channel->component_id, candidates);
      g_slist_foreach (candidates, (GFunc)nice_candidate_free, NULL);
      g_slist_free (candidates);
    }
}

static void
nice_candidate_gathering_done (NiceAgent *agent, guint stream_id,
    gpointer user_data)
{
  GtalkFtManager *self = GTALK_FT_MANAGER (user_data);
  JingleChannel *channel = get_jingle_channel (self, agent);
  GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (channel->content);
  GList *candidates = NULL;
  GList *remote_candidates = NULL;
  GSList *local_candidates;
  GSList *li;

  DEBUG ("libnice candidate gathering done!!!!");

  /* Send remote candidates to libnice and listen to new signal */
  remote_candidates = gabble_jingle_content_get_remote_candidates (content);
  content_new_remote_candidates_cb (content, remote_candidates, self);

  gabble_signal_connect_weak (content, "new-candidates",
      (GCallback) content_new_remote_candidates_cb, G_OBJECT (self));

  /* Send gathered local candidates to the content */
  local_candidates = nice_agent_get_local_candidates (agent, stream_id,
      channel->component_id);

  for (li = local_candidates; li; li = li->next)
    {
      NiceCandidate *cand = li->data;
      JingleCandidate *candidate;
      gchar ip[NICE_ADDRESS_STRING_LEN];

      nice_address_to_string (&cand->addr, ip);

      candidate = jingle_candidate_new (
          /* protocol */
          cand->transport == NICE_CANDIDATE_TRANSPORT_UDP?
          JINGLE_TRANSPORT_PROTOCOL_UDP:
          JINGLE_TRANSPORT_PROTOCOL_TCP,
          /* candidate type */
          cand->type == NICE_CANDIDATE_TYPE_HOST?
          JINGLE_CANDIDATE_TYPE_LOCAL:
          cand->type == NICE_CANDIDATE_TYPE_RELAYED?
          JINGLE_CANDIDATE_TYPE_RELAY:
          JINGLE_CANDIDATE_TYPE_STUN,
          /* id */
          cand->foundation,
          /* component */
          channel->channel_id,
          /* address */
          ip,
          /* port */
          nice_address_get_port (&cand->addr),
          /* generation */
          0,
          /* preference */
          cand->priority / 1000,
          /* username */
          cand->username?cand->username:"",
          /* password */
          cand->password?cand->password:"",
          /* network */
          0);

      candidates = g_list_prepend (candidates, candidate);
    }

  gabble_jingle_content_add_candidates (content, candidates);
}

static void
nice_component_state_changed (NiceAgent *agent,  guint stream_id,
    guint component_id, guint state, gpointer user_data)
{
  GtalkFtManager *self = GTALK_FT_MANAGER (user_data);
  JingleChannel *channel = get_jingle_channel (self, agent);
  GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (channel->content);
  JingleTransportState ts = JINGLE_TRANSPORT_STATE_DISCONNECTED;

  DEBUG ("libnice component state changed %d!!!!", state);

  switch (state)
    {
      case NICE_COMPONENT_STATE_DISCONNECTED:
      case NICE_COMPONENT_STATE_GATHERING:
        ts = JINGLE_TRANSPORT_STATE_DISCONNECTED;
        break;
      case NICE_COMPONENT_STATE_CONNECTING:
        ts = JINGLE_TRANSPORT_STATE_CONNECTING;
        break;
      case NICE_COMPONENT_STATE_CONNECTED:
      case NICE_COMPONENT_STATE_READY:
        ts = JINGLE_TRANSPORT_STATE_CONNECTED;
        break;
      case NICE_COMPONENT_STATE_FAILED:
        {
          GList *i;

          for (i = self->priv->channels; i; i = i->next)
            {
              GabbleChannel *c = i->data;
              gabble_file_transfer_channel_set_gtalk_ft_state (c->channel,
                  CONNECTION_FAILED, LOCAL_ERROR);
            }
          /* return because we don't want to use the content after it
             has been destroyed.. */
          return;
        }
    }
  gabble_jingle_content_set_transport_state (content, ts);
}

static void get_next_manifest_entry (GtalkFtManager *self,
    JingleChannel *channel)
{
  GabbleJingleShareManifest *manifest = NULL;
  GabbleJingleShareManifestEntry *entry = NULL;

  manifest = gabble_jingle_share_get_manifest (channel->content);
  channel->manifest_entry++;
  entry = g_list_nth_data (manifest->entries, channel->manifest_entry);

  /* FIXME: We only support one file per manifest for now! */
  /* TODO */
  if (channel->manifest_entry > 0)
    entry = NULL;

  /* TODO send COMPLETED state to the last active channel */
  if (entry == NULL)
    {
      GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (channel->content);
      DEBUG ("Received all the files. Transfer is complete");
      gabble_jingle_content_send_complete (content);
    }
  else
    {
      gchar *buffer = NULL;
      gchar *source_url = manifest->source_url;
      guint url_len = strlen (source_url);
      gchar *separator = "";

      if (source_url[url_len -1] != '/')
        separator = "/";

      /* The session initiator will always be the full JID of the peer */
      buffer = g_strdup_printf ("GET %s%s%s HTTP/1.1\r\n"
          "Connection: Keep-Alive\r\n"
          "Content-Length: 0\r\n"
          "Host: %s:0\r\n"
          "User-Agent: %s\r\n\r\n",
          source_url, separator, entry->name,
          gabble_jingle_session_get_initiator (self->priv->jingle),
          PACKAGE_STRING);

      /* FIXME: check for success */
      nice_agent_send (channel->agent, channel->stream_id,
          channel->component_id, strlen (buffer), buffer);
      g_free (buffer);

      channel->http_status = HTTP_CLIENT_RECEIVE;
      /* Do not receive anything until the local socket is connected */
      nice_agent_attach_recv (channel->agent, channel->stream_id,
          channel->component_id, NULL, NULL, NULL);
      channel->agent_attached = FALSE;
      /* TODO: send OPEN state to channel so we can attach again */
    }
}

static void
nice_component_writable (NiceAgent *agent, guint stream_id, guint component_id,
    gpointer user_data)
{
  GtalkFtManager *self = GTALK_FT_MANAGER (user_data);
  JingleChannel *channel = get_jingle_channel (self, agent);

  if (channel->http_status == HTTP_CLIENT_IDLE)
    {
      get_next_manifest_entry (self, channel);
    }
  else if (channel->http_status == HTTP_SERVER_SEND)
    {
      /* TODO */
      //gibber_transport_block_receiving (self->priv->transport, FALSE);
      if (channel->write_buffer)
        {
          gint ret = nice_agent_send (agent, stream_id, component_id,
              channel->write_len, channel->write_buffer);
          if (ret < 0 || (guint) ret < channel->write_len)
            {
              gchar *to_free = channel->write_buffer;
              if (ret < 0)
                ret = 0;

              channel->write_buffer = g_memdup (channel->write_buffer + ret,
                  channel->write_len - ret);
              channel->write_len = channel->write_len - ret;
              g_free (to_free);

              /* TODO */
              //gibber_transport_block_receiving (self->priv->transport, TRUE);
            }
          else
            {
              g_free (channel->write_buffer);
              channel->write_buffer = NULL;
              channel->write_len = 0;
            }
        }
    }

}

typedef struct
{
  GtalkFtManager *self;
  JingleChannel *channel;
} GoogleRelaySessionData;

static void
set_relay_info (gpointer item, gpointer user_data)
{
  GoogleRelaySessionData *data = user_data;
  GHashTable *relay = item;
  const gchar *server_ip = NULL;
  const gchar *username = NULL;
  const gchar *password = NULL;
  const gchar *type_str = NULL;
  guint server_port;
  NiceRelayType type;
  GValue *value;

  value = g_hash_table_lookup (relay, "ip");
  if (value)
    server_ip = g_value_get_string (value);
  else
    return;

  value = g_hash_table_lookup (relay, "port");
  if (value)
    server_port = g_value_get_uint (value);
  else
    return;

  value = g_hash_table_lookup (relay, "username");
  if (value)
    username = g_value_get_string (value);
  else
    return;

  value = g_hash_table_lookup (relay, "password");
  if (value)
    password = g_value_get_string (value);
  else
    return;

  value = g_hash_table_lookup (relay, "type");
  if (value)
    type_str = g_value_get_string (value);
  else
    return;

  if (!strcmp (type_str, "udp"))
    type = NICE_RELAY_TYPE_TURN_UDP;
  else if (!strcmp (type_str, "tcp"))
    type = NICE_RELAY_TYPE_TURN_TCP;
  else if (!strcmp (type_str, "tls"))
    type = NICE_RELAY_TYPE_TURN_TLS;
  else
    return;

  nice_agent_set_relay_info (data->channel->agent,
      data->channel->stream_id, data->channel->component_id,
      server_ip, server_port,
      username, password, type);

}

static void
google_relay_session_cb (GPtrArray *relays, gpointer user_data)
{
  GoogleRelaySessionData *data = user_data;

  if (data->self == NULL)
    {
      DEBUG ("Received relay session callback but self got destroyed");
      g_slice_free (GoogleRelaySessionData, data);
      return;
    }

  if (relays)
      g_ptr_array_foreach (relays, set_relay_info, user_data);

  nice_agent_gather_candidates (data->channel->agent, data->channel->stream_id);

  g_object_remove_weak_pointer (G_OBJECT (data->self), (gpointer *)&data->self);
  g_slice_free (GoogleRelaySessionData, data);
}


static void
content_new_channel_cb (GabbleJingleContent *content, const gchar *name,
    gint channel_id, gpointer user_data)
{
  GtalkFtManager *self = GTALK_FT_MANAGER (user_data);
  JingleChannel *channel = g_slice_new0 (JingleChannel);
  NiceAgent *agent = nice_agent_new_reliable (g_main_context_default (),
      NICE_COMPATIBILITY_GOOGLE);
  guint stream_id = nice_agent_add_stream (agent, 1);
  gchar *stun_server;
  guint stun_port;
  GoogleRelaySessionData *relay_data = NULL;

  DEBUG ("New channel %s was created and linked to id %d", name, channel_id);

  channel->agent = agent;
  channel->stream_id = stream_id;
  channel->component_id = NICE_COMPONENT_TYPE_RTP;
  channel->content = GABBLE_JINGLE_SHARE (content);
  channel->channel_id = channel_id;

  /* We increment the manifest_entry before fetching the entry, so we
     initialize it at -1, in order to start the index at 0 */
  channel->manifest_entry = -1;

  if (self->priv->requested)
      channel->http_status = HTTP_SERVER_IDLE;
  else
      channel->http_status = HTTP_CLIENT_IDLE;

  gabble_signal_connect_weak (agent, "candidate-gathering-done",
      G_CALLBACK (nice_candidate_gathering_done), G_OBJECT (self));

  gabble_signal_connect_weak (agent, "component-state-changed",
      G_CALLBACK (nice_component_state_changed), G_OBJECT (self));

  gabble_signal_connect_weak (agent, "reliable-transport-writable",
      G_CALLBACK (nice_component_writable), G_OBJECT (self));


  /* Add the agent to the hash table before gathering candidates in case the
     gathering finishes synchronously, and the callback tries to add local
     candidates to the content, it needs to find the channel id.. */
  g_hash_table_insert (self->priv->jingle_channels,
      GINT_TO_POINTER (channel_id), channel);

  channel->agent_attached = TRUE;
  nice_agent_attach_recv (agent, stream_id, channel->component_id,
      g_main_context_default (), nice_data_received_cb, self);

  if (gabble_jingle_factory_get_stun_server (
          self->priv->jingle_factory, &stun_server, &stun_port))
    {
      g_object_set (agent,
          "stun-server", stun_server,
          "stun-server-port", stun_port,
          NULL);
      g_free (stun_server);
    }

  relay_data = g_slice_new0 (GoogleRelaySessionData);
  relay_data->self = self;
  relay_data->channel = channel;
  g_object_add_weak_pointer (G_OBJECT (relay_data->self),
      (gpointer *)&relay_data->self);
  gabble_jingle_factory_create_google_relay_session (
      self->priv->jingle_factory, 1,
      google_relay_session_cb, relay_data);
}

static void
content_completed (GabbleJingleContent *content, gpointer user_data)
{
  GtalkFtManager *self = GTALK_FT_MANAGER (user_data);
  GList *i;
  /* TODO: multi */

  for (i = self->priv->channels; i; i = i->next)
    {
      GabbleChannel *c = i->data;
      gabble_file_transfer_channel_set_gtalk_ft_state (c->channel,
          COMPLETED, NONE);
    }
}

static void
free_jingle_channel (gpointer data)
{
  JingleChannel *channel = (JingleChannel *) data;

  DEBUG ("Freeing jingle channel");

  if (channel->write_buffer)
    {
      g_free (channel->write_buffer);
      channel->write_buffer = NULL;
    }
  if (channel->read_buffer)
    {
      g_free (channel->read_buffer);
      channel->read_buffer = NULL;
    }
  g_object_unref (channel->agent);
  g_slice_free (JingleChannel, channel);
}

/* Return the pointer at the end of the line or NULL if not \n found */
static gchar *
http_read_line (gchar *buffer, guint len)
{
  gchar *p = memchr (buffer, '\n', len);

  if (p != NULL)
    {
      *p = 0;
      if (p > buffer && *(p-1) == '\r')
        *(p-1) = 0;
      p++;
    }

  return p;
}

static guint
http_data_received (GtalkFtManager *self, JingleChannel *channel,
    gchar *buffer, guint len)
{

  switch (channel->http_status)
    {
      case HTTP_SERVER_IDLE:
        {
          gchar *headers = http_read_line (buffer, len);
          if (headers == NULL)
            return 0;

          channel->http_status = HTTP_SERVER_HEADERS;

          return headers - buffer;
        }
        break;
      case HTTP_SERVER_HEADERS:
        {
          gchar *line = buffer;
          gchar *next_line = http_read_line (buffer, len);
          if (next_line == NULL)
            return 0;

          DEBUG ("Found server headers line (%d) : %s", strlen (line), line);
          if (*line == 0)
            {
              gchar *response = NULL;
              /* FIXME: how about content-length and an actual body ? */
              channel->http_status = HTTP_SERVER_SEND;
              DEBUG ("Found empty line, now sending our response");

              /* FIXME: actually check for the requested file's uri */
              /* FIXME: check for success of nice_agent_send */
              /*TODO*/
              response = g_strdup_printf ("HTTP/1.1 200\r\n"
                  "Connection: Keep-Alive\r\n"
                  "Content-Length: %llu\r\n"
                  "Content-Type: application/octet-stream\r\n\r\n",
                  (guint64) 0/* TODO self->priv->size*/);
              nice_agent_send (channel->agent, channel->stream_id,
                  channel->component_id, strlen (response), response);
              g_free (response);
              /*TODO*/
              //gibber_transport_block_receiving (self->priv->transport, FALSE);
            }

          return next_line - buffer;
        }
        break;
      case HTTP_SERVER_SEND:
        DEBUG ("received data when we're supposed to be sending data.. "
            "not supposed to happen");
        break;
      case HTTP_CLIENT_IDLE:
        DEBUG ("received data when we're supposed to be sending the GET.. "
            "not supposed to happen");
        break;
      case HTTP_CLIENT_RECEIVE:
        {
          gchar *headers = http_read_line (buffer, len);
          if (headers == NULL)
            return 0;

          channel->http_status = HTTP_CLIENT_HEADERS;

          return headers - buffer;
        }
      case HTTP_CLIENT_HEADERS:
        {
          gchar *line = buffer;
          gchar *next_line = http_read_line (buffer, len);
          if (next_line == NULL)
            return 0;

          DEBUG ("Found client headers line (%d) : %s", strlen (line), line);
          if (*line == 0)
            {
              DEBUG ("Found empty line, now receiving file data");
              if (channel->is_chunked)
                {
                  channel->http_status = HTTP_CLIENT_CHUNK_SIZE;
                }
              else
                {
                  channel->http_status = HTTP_CLIENT_BODY;
                  if (channel->content_length == 0)
                    get_next_manifest_entry (self, channel);
                }
            }
          else if (!g_ascii_strncasecmp (line, "Content-Length: ", 16))
            {
              channel->is_chunked = FALSE;
              /* Check strtoull read all the length */
              channel->content_length = g_ascii_strtoull (line + 16,
                  NULL, 10);
              DEBUG ("Found data length : %llu", channel->content_length);
            }
          else if (!g_ascii_strncasecmp (line,
                  "Transfer-Encoding: chunked", 26))
            {
              channel->is_chunked = TRUE;
              channel->content_length = 0;
              DEBUG ("Found file is chunked");
            }

          return next_line - buffer;
        }
        break;
      case HTTP_CLIENT_CHUNK_SIZE:
        {
          gchar *line = buffer;
          gchar *next_line = http_read_line (buffer, len);
          if (next_line == NULL)
            return 0;

          /* FIXME : check validity of strtoul */
          channel->content_length = strtoul (line, NULL, 16);
          if (channel->content_length > 0)
              channel->http_status = HTTP_CLIENT_BODY;
          else
              channel->http_status = HTTP_CLIENT_CHUNK_FINAL;

          /*TODO*/
          /* Reset the size of the file as we receive more data than expected */
          /* if (self->priv->transferred_bytes + self->priv->initial_offset +
              channel->content_length >= self->priv->size)
            self->priv->size = self->priv->transferred_bytes +          \
                self->priv->initial_offset + channel->content_length;
          */
          return next_line - buffer;
        }
        break;
      case HTTP_CLIENT_BODY:
        {
          guint consumed = 0;

          if (len >= channel->content_length)
            {
              consumed = channel->content_length;
              /* TODO */
              //send_transport_data (self, channel, (const guint8 *) buffer,
              //   channel->content_length);
              channel->content_length = 0;
              if (channel->is_chunked)
                channel->http_status = HTTP_CLIENT_CHUNK_END;
              else
                get_next_manifest_entry (self, channel);
            }
          else
            {
              consumed = len;
              channel->content_length -= len;
              /* TODO */
              //send_transport_data (self, channel, (const guint8 *) buffer, len);
            }

          return consumed;
        }
        break;
      case HTTP_CLIENT_CHUNK_END:
        {
          gchar *chunk = http_read_line (buffer, len);
          if (chunk == NULL)
            return 0;

          channel->http_status = HTTP_CLIENT_CHUNK_SIZE;

          return chunk - buffer;
        }
        break;
      case HTTP_CLIENT_CHUNK_FINAL:
        {
          gchar *end = http_read_line (buffer, len);
          if (end == NULL)
            return 0;

          channel->http_status = HTTP_CLIENT_IDLE;
          get_next_manifest_entry (self, channel);

          return end - buffer;
        }
        break;
    }

  return 0;
}

static void
nice_data_received_cb (NiceAgent *agent,
                       guint stream_id,
                       guint component_id,
                       guint len,
                       gchar *buffer,
                       gpointer user_data)
{
  GtalkFtManager *self = GTALK_FT_MANAGER (user_data);
  JingleChannel *channel = get_jingle_channel (self, agent);
  gchar *free_buffer = NULL;


  if (channel->read_buffer != NULL)
    {
      gchar *tmp = g_malloc (channel->read_len + len);
      memcpy (tmp, channel->read_buffer, channel->read_len);
      memcpy (tmp + channel->read_len, buffer, len);

      free_buffer = buffer = tmp;
      len += channel->read_len;

      g_free (channel->read_buffer);
      channel->read_buffer = NULL;
      channel->read_len = 0;
    }
  while (len > 0)
    {
      guint consumed = http_data_received (self, channel, buffer, len);

      if (consumed == 0)
        {
          channel->read_buffer = g_memdup (buffer, len);
          channel->read_len = len;
          break;
        }
      else
        {
          /* we assume http_data_received never returns consumed > len */
          len -= consumed;
          buffer += consumed;
        }
    }

  if (free_buffer != NULL)
    g_free (free_buffer);

}

static void
_hold_session (GtalkFtManager * self,
    GabbleJingleSession *session, GabbleJingleContent *content)
{
  self->priv->jingle = g_object_ref (session);

  gabble_signal_connect_weak (session, "notify::state",
      (GCallback) jingle_session_state_changed_cb, G_OBJECT (self));
  gabble_signal_connect_weak (session, "terminated",
      (GCallback) jingle_session_terminated_cb, G_OBJECT (self));

  gabble_signal_connect_weak (content, "new-channel",
      (GCallback) content_new_channel_cb, G_OBJECT (self));
  gabble_signal_connect_weak (content, "completed",
      (GCallback) content_completed, G_OBJECT (self));
}

static void
_add_channel (GtalkFtManager * self, GabbleFileTransferChannel *channel)
{
  GabbleChannel *c = g_slice_new0 (GabbleChannel);

  c->channel = g_object_ref (channel);
  self->priv->channels = g_list_append (self->priv->channels, c);

  gabble_file_transfer_channel_set_gtalk_ft(channel, self);
}

GtalkFtManager *
gtalk_ft_manager_new (GabbleFileTransferChannel *channel,
    GabbleJingleFactory *jingle_factory, TpHandle handle, const gchar *resource)
{
  GtalkFtManager * self = g_object_new (GTALK_TYPE_FT_MANAGER, NULL);
  GabbleJingleSession *session = NULL;
  GabbleJingleContent *content = NULL;
  gchar *filename;
  guint64 size;

  self->priv->jingle_factory = jingle_factory;
  self->priv->requested = TRUE;

  session = gabble_jingle_factory_create_session (jingle_factory,
      handle, resource, FALSE);

  if (session == NULL)
    {
      g_object_unref (self);
      return NULL;
    }

  g_object_set (session, "dialect", JINGLE_DIALECT_GTALK4, NULL);

  content = gabble_jingle_session_add_content (session,
      JINGLE_MEDIA_TYPE_FILE, "share", NS_GOOGLE_SESSION_SHARE,
      NS_GOOGLE_TRANSPORT_P2P);

  if (content == NULL)
    {
      g_object_unref (self);
      g_object_unref (session);
      return NULL;
    }

  g_object_get (channel,
      "filename", &filename,
      "size", &size,
      NULL);
  g_object_set (content,
      "filename", filename,
      "filesize", size,
      NULL);

  _hold_session (self, session, content);

  _add_channel (self, channel);

  return self;
}

GtalkFtManager *
gtalk_ft_manager_new_from_session (GabbleJingleFactory *jingle_factory,
    GabbleJingleSession *session)
{
  GtalkFtManager * self = g_object_new (GTALK_TYPE_FT_MANAGER, NULL);
  GabbleJingleContent *content = NULL;
  GList *cs;

  self->priv->jingle_factory = jingle_factory;
  self->priv->requested = FALSE;

  cs = gabble_jingle_session_get_contents (session);

  if (cs != NULL)
    {
      content = GABBLE_JINGLE_CONTENT (cs->data);

      if (content == NULL)
        {
          g_object_unref (self);
          return NULL;
        }
    }

  _hold_session (self, session, content);

    /* TODO */
  /*
  {
    GabbleJingleShareManifest *manifest = NULL;
    GabbleJingleShareManifestEntry *entry = NULL;

    manifest = gabble_jingle_share_get_manifest (content);
    entry = g_list_nth_data (manifest->entries, 0);
    if (entry != NULL)
      {
        g_free (self->priv->filename);
        if (entry->folder)
          self->priv->filename = g_strdup_printf ("%s.tar", entry->name);
        else
          self->priv->filename = g_strdup (entry->name);

        self->priv->size = entry->size;
      }
  }
  */

  return self;
}

void
gtalk_ft_manager_initiate (GtalkFtManager *self,
    GabbleFileTransferChannel * channel)
{
  gabble_jingle_session_accept (self->priv->jingle);
}

void
gtalk_ft_manager_accept (GtalkFtManager *self,
    GabbleFileTransferChannel * channel)
{
  GList *cs = gabble_jingle_session_get_contents (self->priv->jingle);

  if (cs != NULL)
    {
      GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (cs->data);
      guint initial_id = 0;
      gint channel_id;

      /* The new-channel signal will take care of the rest.. */
      do
        {
          gchar *channel_name = NULL;

          channel_name = g_strdup_printf ("gabble-%d", ++initial_id);
          channel_id = gabble_jingle_content_create_channel (content,
              channel_name);
          g_free (channel_name);
        } while (channel_id <= 0 && initial_id < 10);

      /* FIXME: not assert but actually cancel the FT? */
      g_assert (channel_id > 0);
    }
  gabble_jingle_session_accept (self->priv->jingle);
}

gint
gtalk_ft_manager_send_data (GtalkFtManager *self,
    const gchar *data, guint length)
{

  JingleChannel *channel = g_hash_table_lookup (self->priv->jingle_channels,
      GINT_TO_POINTER (1));
  gint ret = nice_agent_send (channel->agent, channel->stream_id,
      channel->component_id, length, data);

  if (ret < 0 || (guint) ret < length)
    {
      if (ret < 0)
        ret = 0;

      channel->write_buffer = g_memdup (data + ret,
          length - ret);
      channel->write_len = length - ret;
      /*TODO*/
      //gibber_transport_block_receiving (self->priv->transport, TRUE);
    }
  return ret;
}

void
gtalk_ft_manager_block_reading (GtalkFtManager *self, gboolean block)
{
  JingleChannel *channel = g_hash_table_lookup (self->priv->jingle_channels,
      GINT_TO_POINTER (1));
  if (block)
    {
      if (channel && !channel->agent_attached)
        {
          channel->agent_attached = TRUE;
          nice_agent_attach_recv (channel->agent, channel->stream_id,
              channel->component_id, g_main_context_default (),
              nice_data_received_cb, self);
        }
    }
  else
    {
      if (channel && channel->agent_attached)
        {
          nice_agent_attach_recv (channel->agent, channel->stream_id,
              channel->component_id, NULL, NULL, NULL);
          channel->agent_attached = FALSE;
        }
    }
}

void
gtalk_ft_manager_terminate (GtalkFtManager *self,
    GabbleFileTransferChannel * channel)
{
}
