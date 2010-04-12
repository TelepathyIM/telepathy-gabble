/*
 * gtalk-file-collection.c - Source for GTalkFileCollection
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


#include "gtalk-file-collection.h"

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

/*
 * This GTalk compatible file transfer protocol is a bit complicated, so here
 * is an explanation on how it works :
 *
 * A pseudo-good initial source of information is available here :
 * http://code.google.com/apis/talk/libjingle/file_share.html
 *
 * The current object interaction is like this :
 *
 *                GabbleFileTransferChannelManager
 *                               |
 *                               |
 *                               |
 *                               |
 *                               * ref
 *                     GabbleFileTransferChannel
 *                weakref *         *
 *                       /           \
 *                      /             \
 *                     /               \
 *                    /                 \
 *                   /                   \
 *                  /                     \
 *                 /                       \
 *            ref /                         \
 *      GTalkFileCollection                  \
 *              |                             \
 *              |                              \
 *          ref |                               \
 *     GabbleJingleSession                       \
 *              |                                 \ (one at a time)
 *              |                                  \
 *          ref |                                   \
 *     GabbleJingleShare  ------------------*- ShareChannel -------- NiceAgent
 *              |                                                       |
 *              |                                                       |
 *         ref  |                           ref                         |
 *   GabbleTransportGoogle ----------------*- JingleCandidate        PseudoTCP
 *
 * The protocol works like this :
 * Once you receive an invitation, the manifest will contain a number of files
 * and folders. Some files might have image attributes (width/height) that help
 * specify that they are images.
 * If there are images in the invitation, a new ShareChannel gets created and
 * connectivity must be established. Then on that stream, an HTTP GET request
 * is sent for each image being transfered with the URL as :
 * GET <preview-path>/filename?width=X&height=Y
 * where X and Y are the thumbnail's requested width and height.
 * The peer should at this point scale down the image to the requested width and
 * height and send the thumbnail for showing the preview of the image in the FT
 * UI.
 * Once the invitation is accepted, a new ShareChannel is created, which will
 * cause a new NiceAgent to be created and connectivity to be established on that
 * ShareChannel. The resulting stream is then used as an HTTP server/client to
 * request files on it using the <source-path> as prefix to the URL.
 * Simple files are being transferred normally, while directories will be
 * transferred as a tarball with 'chuncked' Transfer-Encoding since the resulting
 * size of the tarball isn't known in advance.
 * Once a file is completely transferred, then the next file is requested on the
 * same ShareChannel. If all files are transferred, then the <complete> info
 * action is being sent through the jingle signaling, and the session can then
 * be terminated safely.
 *
 * Since telepathy doesn't currently support image previews, so the 'preview'
 * ShareChannel is never created with gabble.
 * Also note that we only create one ShareChannel and we serialize the file
 * transfers one after the other, they do not each get one ShareChannel and they
 * cannot be downloaded in parallel.
 *
 */

G_DEFINE_TYPE (GTalkFileCollection, gtalk_file_collection, G_TYPE_OBJECT);

/* properties */
enum
{
  PROP_TOKEN = 1,
  LAST_PROPERTY
};

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
  guint share_channel_id;
  HttpStatus http_status;
  gchar *status_line;
  gboolean is_chunked;
  guint64 content_length;
  gchar *write_buffer;
  guint write_len;
  gchar *read_buffer;
  guint read_len;
} ShareChannel;


typedef enum
{
  GTALK_FT_STATUS_PENDING,
  GTALK_FT_STATUS_INITIATED,
  GTALK_FT_STATUS_ACCEPTED,
  GTALK_FT_STATUS_TRANSFERRING,
  GTALK_FT_STATUS_WAITING,
  GTALK_FT_STATUS_TERMINATED
} GtalkFtStatus;

struct _GTalkFileCollectionPrivate
{
  gboolean dispose_has_run;

  GtalkFtStatus status;
  /* GList of weakreffed GabbleFileTransferChannel */
  GList *channels;
  /* GHashTable of GabbleFileTransferChannel => GINT_TO_POINTER (gboolean) */
  /* the weakref to the channel here is held through the GList *channels */
  GHashTable *channels_reading;
  /* GHashTable of GabbleFileTransferChannel => GINT_TO_POINTER (gboolean) */
  /* the weakref to the channel here is held through the GList *channels */
  GHashTable *channels_usable;
  GabbleFileTransferChannel *current_channel;
  GabbleJingleFactory *jingle_factory;
  GabbleJingleSession *jingle;
  /* ICE component id to jingle share channel association
     GINT_TO_POINTER (candidate->component) => g_slice_new (ShareChannel) */
  GHashTable *share_channels;
  gboolean requested;
  gchar *token;
};

static void free_share_channel (gpointer data);
static void nice_data_received_cb (NiceAgent *agent,
    guint stream_id, guint component_id, guint len, gchar *buffer,
    gpointer user_data);
static void set_current_channel (GTalkFileCollection *self,
    GabbleFileTransferChannel *channel);
static void channel_disposed (gpointer data, GObject *where_the_object_was);

static void
gtalk_file_collection_init (GTalkFileCollection *self)
{
  GTalkFileCollectionPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (self, GTALK_TYPE_FILE_COLLECTION,
         GTalkFileCollectionPrivate);
  gchar buf[16];
  guint32 *uint_buf = (guint32 *) buf;
  guint i;


  DEBUG ("GTalk file collection init called");
  self->priv = priv;

  self->priv->status = GTALK_FT_STATUS_PENDING;

  self->priv->channels_reading = g_hash_table_new_full (NULL, NULL, NULL, NULL);
  self->priv->channels_usable = g_hash_table_new_full (NULL, NULL, NULL, NULL);

  self->priv->share_channels = g_hash_table_new_full (NULL, NULL,
      NULL, free_share_channel);

  for (i = 0; i < sizeof (buf); i++)
    buf[i] = g_random_int_range (0, 256);

  self->priv->token = g_strdup_printf ("%x%x%x%x",
      uint_buf[0], uint_buf[1], uint_buf[2], uint_buf[3]);

  /* FIXME: we should start creating a nice agent already and have it start
     the candidate gathering.. but we don't know which jingle-share transport
     channel name to assign it to... */

  priv->dispose_has_run = FALSE;
}


static void
gtalk_file_collection_dispose (GObject *object)
{
  GTalkFileCollection *self = GTALK_FILE_COLLECTION (object);
  GList *i;

  if (self->priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  self->priv->dispose_has_run = TRUE;

  if (self->priv->jingle != NULL)
    {
      gabble_jingle_session_terminate (self->priv->jingle,
          TP_CHANNEL_GROUP_CHANGE_REASON_NONE, NULL, NULL);

      /* the terminate could synchronously unref it and set it to NULL */
      if (self->priv->jingle != NULL)
        {
          g_object_unref (self->priv->jingle);
          self->priv->jingle = NULL;
        }
    }

  set_current_channel (self, NULL);

  if (self->priv->channels_reading != NULL)
    {
      g_hash_table_destroy (self->priv->channels_reading);
      self->priv->channels_reading = NULL;
    }

  if (self->priv->channels_usable != NULL)
    {
      g_hash_table_destroy (self->priv->channels_usable);
      self->priv->channels_usable = NULL;
    }

  if (self->priv->share_channels != NULL)
    {
      g_hash_table_destroy (self->priv->share_channels);
      self->priv->share_channels = NULL;
    }

  for (i = self->priv->channels; i; i = i->next)
    {
      GabbleFileTransferChannel *channel = i->data;
      g_object_weak_unref (G_OBJECT (channel), channel_disposed, self);
    }
  g_list_free (self->priv->channels);

  g_free (self->priv->token);

  if (G_OBJECT_CLASS (gtalk_file_collection_parent_class)->dispose)
    G_OBJECT_CLASS (gtalk_file_collection_parent_class)->dispose (object);
}

static void
gtalk_file_collection_get_property (GObject *object,
                                           guint property_id,
                                           GValue *value,
                                           GParamSpec *pspec)
{
  GTalkFileCollection *self = GTALK_FILE_COLLECTION (object);

  switch (property_id)
    {
      case PROP_TOKEN:
        g_value_set_string (value, self->priv->token);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}


static void
gtalk_file_collection_class_init (GTalkFileCollectionClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  g_type_class_add_private (cls, sizeof (GTalkFileCollectionPrivate));

  object_class->get_property = gtalk_file_collection_get_property;
  object_class->dispose = gtalk_file_collection_dispose;

  g_object_class_install_property (object_class, PROP_TOKEN,
      g_param_spec_string (
          "token",
          "Unique token identifiying the FileCollection",
          "Token identifying a collection of files",
          "",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}


static ShareChannel *
get_share_channel (GTalkFileCollection *self, NiceAgent *agent)
{
  GHashTableIter iter;
  gpointer key, value;
  ShareChannel *ret = NULL;

  g_hash_table_iter_init (&iter, self->priv->share_channels);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      ShareChannel *share_channel = (ShareChannel *) value;
      if (share_channel->agent == agent)
        {
          ret = share_channel;
          break;
        }
    }

  return ret;
}


static GabbleFileTransferChannel *
get_channel_by_filename (GTalkFileCollection *self, gchar *filename)
{
  GList *i;

  for (i = self->priv->channels; i; i = i->next)
    {
      GabbleFileTransferChannel *channel = i->data;
      gchar *file = NULL;

      g_object_get (channel,
          "filename", &file,
          NULL);

      if (strcmp (file, filename) == 0)
        return channel;
    }

  return NULL;
}

static void
set_current_channel (GTalkFileCollection *self,
    GabbleFileTransferChannel *channel)
{
  self->priv->current_channel = channel;

  if (channel != NULL)
    {
      gboolean reading = FALSE;

      gabble_file_transfer_channel_gtalk_file_collection_state_changed (
          channel, GTALK_FILE_COLLECTION_STATE_OPEN, FALSE);
      reading = GPOINTER_TO_INT (g_hash_table_lookup (
              self->priv->channels_reading, channel));
      gtalk_file_collection_block_reading (self, channel, !reading);
    }
}

static gboolean
channel_exists (GTalkFileCollection * self, GabbleFileTransferChannel *channel)
{
  GList *i;

  for (i = self->priv->channels; i; i = i->next)
    {
      if (channel == i->data)
        return TRUE;
    }

  return FALSE;
}

static void
add_channel (GTalkFileCollection * self, GabbleFileTransferChannel *channel)
{
  self->priv->channels = g_list_append (self->priv->channels, channel);
  g_hash_table_replace (self->priv->channels_reading, channel,
      GINT_TO_POINTER (FALSE));
  g_object_weak_ref (G_OBJECT (channel), channel_disposed, self);
}

static void
del_channel (GTalkFileCollection * self, GabbleFileTransferChannel *channel)
{
  g_return_if_fail (channel_exists (self, channel));

  self->priv->channels = g_list_remove (self->priv->channels, channel);
  g_hash_table_remove (self->priv->channels_reading, channel);
  g_hash_table_remove (self->priv->channels_usable, channel);
  g_object_weak_unref (G_OBJECT (channel), channel_disposed, self);
  if (self->priv->current_channel == channel)
    set_current_channel (self, NULL);
}

static void
jingle_session_state_changed_cb (GabbleJingleSession *session,
                                 GParamSpec *arg1,
                                 GTalkFileCollection *self)
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
        for (i = self->priv->channels; i;)
          {
            GabbleFileTransferChannel *channel = i->data;

            i = i->next;
            gabble_file_transfer_channel_gtalk_file_collection_state_changed (
                channel, GTALK_FILE_COLLECTION_STATE_PENDING, FALSE);
          }
        break;
      case JS_STATE_PENDING_ACCEPT_SENT:
      case JS_STATE_ACTIVE:
        /* Do not set the channels to OPEN unless we're ready to send/receive
           data from them */
        if (self->priv->status == GTALK_FT_STATUS_INITIATED)
          self->priv->status = GTALK_FT_STATUS_ACCEPTED;
        for (i = self->priv->channels; i;)
          {
            GabbleFileTransferChannel *channel = i->data;
            gboolean usable;

            i = i->next;

            usable = GPOINTER_TO_INT (g_hash_table_lookup (
                    self->priv->channels_usable, channel));
            if (usable)
              gabble_file_transfer_channel_gtalk_file_collection_state_changed (
                  channel, GTALK_FILE_COLLECTION_STATE_ACCEPTED, FALSE);
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
  GTalkFileCollection *self = GTALK_FILE_COLLECTION (user_data);
  GList *i;

  g_assert (session == self->priv->jingle);

  self->priv->status = GTALK_FT_STATUS_TERMINATED;

  for (i = self->priv->channels; i;)
    {
      GabbleFileTransferChannel *channel = i->data;

      i = i->next;
      gabble_file_transfer_channel_gtalk_file_collection_state_changed (
          channel, GTALK_FILE_COLLECTION_STATE_TERMINATED, local_terminator);
    }
}

static void
content_new_remote_candidates_cb (GabbleJingleContent *content,
    GList *clist, gpointer user_data)
{
  GTalkFileCollection *self = GTALK_FILE_COLLECTION (user_data);
  GList *li;

  DEBUG ("Got new remote candidates");

  for (li = clist; li; li = li->next)
    {
      JingleCandidate *candidate = li->data;
      NiceCandidate *cand = NULL;
      ShareChannel *share_channel = NULL;
      GSList *candidates = NULL;

      if (candidate->type != JINGLE_TRANSPORT_PROTOCOL_UDP)
        continue;

      share_channel = g_hash_table_lookup (self->priv->share_channels,
          GINT_TO_POINTER (candidate->component));
      if (share_channel == NULL)
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
      cand->stream_id = share_channel->stream_id;
      cand->component_id = share_channel->component_id;
      /*
      if (c->id == NULL)
        candidate_id = g_strdup_printf ("R%d", ++priv->remote_candidate_count);
      else
      candidate_id = c->id;*/
      if (candidate->id != NULL)
        strncpy (cand->foundation, candidate->id,
            NICE_CANDIDATE_MAX_FOUNDATION - 1);
      cand->username = g_strdup (candidate->username?candidate->username:"");
      cand->password = g_strdup (candidate->password?candidate->password:"");

      candidates = g_slist_append (candidates, cand);
      nice_agent_set_remote_candidates (share_channel->agent,
          share_channel->stream_id, share_channel->component_id, candidates);
      g_slist_foreach (candidates, (GFunc)nice_candidate_free, NULL);
      g_slist_free (candidates);
    }
}

static void
nice_candidate_gathering_done (NiceAgent *agent, guint stream_id,
    gpointer user_data)
{
  GTalkFileCollection *self = GTALK_FILE_COLLECTION (user_data);
  ShareChannel *share_channel = get_share_channel (self, agent);
  GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (share_channel->content);
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
      share_channel->component_id);

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
          share_channel->share_channel_id,
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
  GTalkFileCollection *self = GTALK_FILE_COLLECTION (user_data);
  ShareChannel *share_channel = get_share_channel (self, agent);
  GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (share_channel->content);
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

          for (i = self->priv->channels; i;)
            {
              GabbleFileTransferChannel *channel = i->data;

              i = i->next;
              gabble_file_transfer_channel_gtalk_file_collection_state_changed (
                  channel, GTALK_FILE_COLLECTION_STATE_CONNECTION_FAILED,
                  TRUE);
            }
          /* return because we don't want to use the content after it
             has been destroyed.. */
          return;
        }
    }
  gabble_jingle_content_set_transport_state (content, ts);
}

static void
get_next_manifest_entry (GTalkFileCollection *self,
    ShareChannel *share_channel, gboolean error)
{
  GabbleJingleShareManifest *manifest = NULL;
  GabbleJingleShareManifestEntry *entry = NULL;
  GabbleFileTransferChannel *channel = NULL;
  GList *i;

  DEBUG ("called");

  if (self->priv->current_channel != NULL)
    {
      if (g_list_length (self->priv->channels) == 1)
        {
          GabbleJingleContent *content = \
              GABBLE_JINGLE_CONTENT (share_channel->content);

          DEBUG ("Received all the files. Transfer is complete");
          gabble_jingle_content_send_complete (content);
        }

      g_hash_table_replace (self->priv->channels_usable, channel,
          GINT_TO_POINTER (FALSE));
      gabble_file_transfer_channel_gtalk_file_collection_state_changed (
          self->priv->current_channel,
          error ? GTALK_FILE_COLLECTION_STATE_ERROR:
          GTALK_FILE_COLLECTION_STATE_COMPLETED, FALSE);

      set_current_channel (self, NULL);
    }

  manifest = gabble_jingle_share_get_manifest (share_channel->content);
  for (i = manifest->entries; i; i = i->next)
    {
      gchar *filename = NULL;
      gboolean usable;

      entry = i->data;

      filename = g_strdup_printf ("%s%s",
          entry->name, entry->folder? ".tar":"");
      channel = get_channel_by_filename (self, filename);
      g_free (filename);
      if (channel != NULL)
        {
          usable = GPOINTER_TO_INT (g_hash_table_lookup (
                  self->priv->channels_usable, channel));
          if (usable)
            break;
        }
      entry = NULL;
    }

  self->priv->status = GTALK_FT_STATUS_WAITING;


  if (entry != NULL)
    {
      gchar *buffer = NULL;
      gchar *source_url = manifest->source_url;
      guint url_len = strlen (source_url);
      gchar *separator = "";
      gchar *filename = NULL;

      if (source_url[url_len -1] != '/')
        separator = "/";

      self->priv->status = GTALK_FT_STATUS_TRANSFERRING;

      filename = g_uri_escape_string (entry->name, NULL, TRUE);

      /* The session initiator will always be the full JID of the peer */
      buffer = g_strdup_printf ("GET %s%s%s HTTP/1.1\r\n"
          "Connection: Keep-Alive\r\n"
          "Content-Length: 0\r\n"
          "Host: %s:0\r\n"
          "User-Agent: %s\r\n\r\n",
          source_url, separator, filename,
          gabble_jingle_session_get_initiator (self->priv->jingle),
          PACKAGE_STRING);
      g_free (filename);

      /* FIXME: check for success */
      nice_agent_send (share_channel->agent, share_channel->stream_id,
          share_channel->component_id, strlen (buffer), buffer);
      g_free (buffer);

      share_channel->http_status = HTTP_CLIENT_RECEIVE;
      /* Block or unblock accordingly */
      set_current_channel (self, channel);
    }
}

static void
nice_component_writable (NiceAgent *agent, guint stream_id, guint component_id,
    gpointer user_data)
{
  GTalkFileCollection *self = GTALK_FILE_COLLECTION (user_data);
  ShareChannel *share_channel = get_share_channel (self, agent);

  if (share_channel->http_status == HTTP_CLIENT_IDLE)
    {
      get_next_manifest_entry (self, share_channel, FALSE);
    }
  else if (share_channel->http_status == HTTP_SERVER_SEND)
    {
      if (self->priv->current_channel == NULL)
        {
          GList *i;

          DEBUG ("Unexpected current_channel == NULL!");
          for (i = self->priv->channels; i;)
            {
              GabbleFileTransferChannel *channel = i->data;

              i = i->next;
              gabble_file_transfer_channel_gtalk_file_collection_state_changed (
                  channel, GTALK_FILE_COLLECTION_STATE_ERROR, FALSE);
            }
          return;
        }
      gabble_file_transfer_channel_gtalk_file_collection_write_blocked (
          self->priv->current_channel, FALSE);
      if (share_channel->write_buffer != NULL)
        {
          gint ret = nice_agent_send (agent, stream_id, component_id,
              share_channel->write_len, share_channel->write_buffer);

          if (ret < 0 || (guint) ret < share_channel->write_len)
            {
              gchar *to_free = share_channel->write_buffer;

              if (ret < 0)
                ret = 0;

              share_channel->write_buffer = g_memdup (
                  share_channel->write_buffer + ret,
                  share_channel->write_len - ret);
              share_channel->write_len = share_channel->write_len - ret;
              g_free (to_free);

              gabble_file_transfer_channel_gtalk_file_collection_write_blocked (
                  self->priv->current_channel, TRUE);
            }
          else
            {
              g_free (share_channel->write_buffer);
              share_channel->write_buffer = NULL;
              share_channel->write_len = 0;
            }
        }
    }

}

typedef struct
{
  union {
    gpointer ptr;
    GTalkFileCollection *self;
  } u;
  ShareChannel *share_channel;
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
  if (value != NULL)
    server_ip = g_value_get_string (value);
  else
    return;

  value = g_hash_table_lookup (relay, "port");
  if (value != NULL)
    server_port = g_value_get_uint (value);
  else
    return;

  value = g_hash_table_lookup (relay, "username");
  if (value != NULL)
    username = g_value_get_string (value);
  else
    return;

  value = g_hash_table_lookup (relay, "password");
  if (value != NULL)
    password = g_value_get_string (value);
  else
    return;

  value = g_hash_table_lookup (relay, "type");
  if (value != NULL)
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

  nice_agent_set_relay_info (data->share_channel->agent,
      data->share_channel->stream_id, data->share_channel->component_id,
      server_ip, server_port,
      username, password, type);

}

static void
google_relay_session_cb (GPtrArray *relays, gpointer user_data)
{
  GoogleRelaySessionData *data = user_data;

  if (data->u.self == NULL)
    {
      DEBUG ("Received relay session callback but self got destroyed");
      g_slice_free (GoogleRelaySessionData, data);
      return;
    }

  if (relays != NULL)
      g_ptr_array_foreach (relays, set_relay_info, user_data);

  nice_agent_gather_candidates (data->share_channel->agent,
      data->share_channel->stream_id);

  g_object_remove_weak_pointer (G_OBJECT (data->u.self), &data->u.ptr);
  g_slice_free (GoogleRelaySessionData, data);
}


static void
content_new_share_channel_cb (GabbleJingleContent *content, const gchar *name,
    guint share_channel_id, gpointer user_data)
{
  GTalkFileCollection *self = GTALK_FILE_COLLECTION (user_data);
  ShareChannel *share_channel = g_slice_new0 (ShareChannel);
  NiceAgent *agent = nice_agent_new_reliable (g_main_context_default (),
      NICE_COMPATIBILITY_GOOGLE);
  guint stream_id = nice_agent_add_stream (agent, 1);
  gchar *stun_server;
  guint stun_port;
  GoogleRelaySessionData *relay_data = NULL;

  DEBUG ("New Share channel %s was created and linked to id %d", name,
      share_channel_id);

  share_channel->agent = agent;
  share_channel->stream_id = stream_id;
  share_channel->component_id = NICE_COMPONENT_TYPE_RTP;
  share_channel->content = GABBLE_JINGLE_SHARE (content);
  share_channel->share_channel_id = share_channel_id;

  if (self->priv->requested)
      share_channel->http_status = HTTP_SERVER_IDLE;
  else
      share_channel->http_status = HTTP_CLIENT_IDLE;

  gabble_signal_connect_weak (agent, "candidate-gathering-done",
      G_CALLBACK (nice_candidate_gathering_done), G_OBJECT (self));

  gabble_signal_connect_weak (agent, "component-state-changed",
      G_CALLBACK (nice_component_state_changed), G_OBJECT (self));

  gabble_signal_connect_weak (agent, "reliable-transport-writable",
      G_CALLBACK (nice_component_writable), G_OBJECT (self));


  /* Add the agent to the hash table before gathering candidates in case the
     gathering finishes synchronously, and the callback tries to add local
     candidates to the content, it needs to find the share channel id.. */
  g_hash_table_insert (self->priv->share_channels,
      GINT_TO_POINTER (share_channel_id), share_channel);

  share_channel->agent_attached = TRUE;
  nice_agent_attach_recv (agent, stream_id, share_channel->component_id,
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
  relay_data->u.self = self;
  relay_data->share_channel = share_channel;
  g_object_add_weak_pointer (G_OBJECT (relay_data->u.self),
      &relay_data->u.ptr);
  gabble_jingle_factory_create_google_relay_session (
      self->priv->jingle_factory, 1,
      google_relay_session_cb, relay_data);
}

static void
content_completed (GabbleJingleContent *content, gpointer user_data)
{
  GTalkFileCollection *self = GTALK_FILE_COLLECTION (user_data);
  GList *i;

  DEBUG ("Received content completed");

  for (i = self->priv->channels; i;)
    {
      GabbleFileTransferChannel *channel = i->data;

      i = i->next;
      gabble_file_transfer_channel_gtalk_file_collection_state_changed (
          channel, GTALK_FILE_COLLECTION_STATE_COMPLETED, FALSE);
    }
}

static void
free_share_channel (gpointer data)
{
  ShareChannel *share_channel = (ShareChannel *) data;

  DEBUG ("Freeing jingle Share channel");

  if (share_channel->write_buffer != NULL)
    {
      g_free (share_channel->write_buffer);
      share_channel->write_buffer = NULL;
    }
  if (share_channel->read_buffer != NULL)
    {
      g_free (share_channel->read_buffer);
      share_channel->read_buffer = NULL;
    }
  g_object_unref (share_channel->agent);
  g_slice_free (ShareChannel, share_channel);
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
http_data_received (GTalkFileCollection *self, ShareChannel *share_channel,
    gchar *buffer, guint len)
{

  switch (share_channel->http_status)
    {
      case HTTP_SERVER_IDLE:
        {
          gchar *headers = http_read_line (buffer, len);

          if (headers == NULL)
            return 0;

          share_channel->http_status = HTTP_SERVER_HEADERS;
          share_channel->status_line = g_strdup (buffer);

          if (self->priv->current_channel != NULL)
            {
              DEBUG ("Received status line with current channel set");
              gabble_file_transfer_channel_gtalk_file_collection_state_changed (
                  self->priv->current_channel,
                  GTALK_FILE_COLLECTION_STATE_COMPLETED, FALSE);
              set_current_channel (self, NULL);
            }

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
          /* FIXME: how about content-length and an actual body ? */
          if (*line == 0)
            {
              gchar *response = NULL;
              gchar *get_line = NULL;
              GabbleJingleShareManifest *manifest = NULL;
              gchar *source_url = NULL;
              guint url_len;
              gchar *separator = "";
              gchar *filename = NULL;
              GabbleFileTransferChannel *channel = NULL;

              g_assert (self->priv->current_channel == NULL);

              DEBUG ("Found empty line, received request : %s ",
                  share_channel->status_line);

              manifest = gabble_jingle_share_get_manifest (
                  share_channel->content);
              source_url = manifest->source_url;
              url_len = strlen (source_url);
              if (source_url[url_len -1] != '/')
                separator = "/";

              get_line = g_strdup_printf ("GET %s%s%%s HTTP/1.1",
                  source_url, separator);
              filename = g_malloc (strlen (share_channel->status_line));

              if (sscanf (share_channel->status_line, get_line, filename) == 1)
                {
                  gchar *unescaped = g_uri_unescape_string (filename, NULL);

                  g_free (filename);
                  filename = unescaped;
                  channel = get_channel_by_filename (self, filename);
                }

              if (channel != NULL)
                {
                  guint64 size;

                  g_object_get (channel,
                      "size", &size,
                      NULL);

                  DEBUG ("Found valid filename, result : 200");

                  share_channel->http_status = HTTP_SERVER_SEND;
                  response = g_strdup_printf ("HTTP/1.1 200\r\n"
                      "Connection: Keep-Alive\r\n"
                      "Content-Length: %llu\r\n"
                      "Content-Type: application/octet-stream\r\n\r\n",
                      size);

                }
              else
                {
                  DEBUG ("Unable to find valid filename (%s), result : 404",
                      filename);

                  share_channel->http_status = HTTP_SERVER_IDLE;
                  response = g_strdup_printf ("HTTP/1.1 404\r\n"
                      "Connection: Keep-Alive\r\n"
                      "Content-Length: 0\r\n\r\n");
                }

              /* FIXME: check for success of nice_agent_send */
              nice_agent_send (share_channel->agent, share_channel->stream_id,
                  share_channel->component_id, strlen (response), response);

              g_free (response);
              g_free (filename);
              g_free (get_line);

              /* Now that we sent our response, we can assign the current
                 channel which sets it to OPEN (if non NULL) so data can
                 start flowing */
              self->priv->status = GTALK_FT_STATUS_TRANSFERRING;
              set_current_channel (self, channel);
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

          share_channel->http_status = HTTP_CLIENT_HEADERS;
          share_channel->status_line = g_strdup (buffer);

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
              if (g_str_has_prefix (share_channel->status_line,
                      "HTTP/1.1 200"))
                {
                  if (share_channel->is_chunked)
                    {
                      share_channel->http_status = HTTP_CLIENT_CHUNK_SIZE;
                    }
                  else
                    {
                      share_channel->http_status = HTTP_CLIENT_BODY;
                      if (share_channel->content_length == 0)
                        get_next_manifest_entry (self, share_channel, FALSE);
                    }
                }
              else
                {
                  get_next_manifest_entry (self, share_channel, TRUE);
                }
            }
          else if (!g_ascii_strncasecmp (line, "Content-Length: ", 16))
            {
              share_channel->is_chunked = FALSE;
              /* Check strtoull read all the length */
              share_channel->content_length = g_ascii_strtoull (line + 16,
                  NULL, 10);
              DEBUG ("Found data length : %llu", share_channel->content_length);
            }
          else if (!g_ascii_strncasecmp (line,
                  "Transfer-Encoding: chunked", 26))
            {
              share_channel->is_chunked = TRUE;
              share_channel->content_length = 0;
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
          share_channel->content_length = strtoul (line, NULL, 16);
          if (share_channel->content_length > 0)
              share_channel->http_status = HTTP_CLIENT_BODY;
          else
              share_channel->http_status = HTTP_CLIENT_CHUNK_FINAL;


          return next_line - buffer;
        }
        break;
      case HTTP_CLIENT_BODY:
        {
          guint consumed = 0;

          if (len >= share_channel->content_length)
            {
              if (self->priv->current_channel == NULL)
                {
                  GList *i;

                  DEBUG ("Unexpected current_channel == NULL!");
                  for (i = self->priv->channels; i;)
                    {
                      GabbleFileTransferChannel *channel = i->data;

                      i = i->next;
                      gabble_file_transfer_channel_gtalk_file_collection_state_changed (
                          channel, GTALK_FILE_COLLECTION_STATE_ERROR, FALSE);
                    }
                  /* FIXME: Who knows what might happen here if we got destroyed
                     It shouldn't crash since our object isn't dereferences
                     anymore, but.. */
                  return len;
                }
              consumed = share_channel->content_length;
              gabble_file_transfer_channel_gtalk_file_collection_data_received (
                  self->priv->current_channel, buffer, consumed);
              share_channel->content_length = 0;
              if (share_channel->is_chunked)
                share_channel->http_status = HTTP_CLIENT_CHUNK_END;
              else
                get_next_manifest_entry (self, share_channel, FALSE);
            }
          else
            {
              consumed = len;
              share_channel->content_length -= len;
              gabble_file_transfer_channel_gtalk_file_collection_data_received (
                  self->priv->current_channel, buffer, consumed);
            }

          return consumed;
        }
        break;
      case HTTP_CLIENT_CHUNK_END:
        {
          gchar *chunk = http_read_line (buffer, len);

          if (chunk == NULL)
            return 0;

          share_channel->http_status = HTTP_CLIENT_CHUNK_SIZE;

          return chunk - buffer;
        }
        break;
      case HTTP_CLIENT_CHUNK_FINAL:
        {
          gchar *end = http_read_line (buffer, len);

          if (end == NULL)
            return 0;

          share_channel->http_status = HTTP_CLIENT_IDLE;
          get_next_manifest_entry (self, share_channel, FALSE);

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
  GTalkFileCollection *self = GTALK_FILE_COLLECTION (user_data);
  ShareChannel *share_channel = get_share_channel (self, agent);
  gchar *free_buffer = NULL;

  if (share_channel->read_buffer != NULL)
    {
      gchar *tmp = g_malloc (share_channel->read_len + len);

      memcpy (tmp, share_channel->read_buffer, share_channel->read_len);
      memcpy (tmp + share_channel->read_len, buffer, len);

      free_buffer = buffer = tmp;
      len += share_channel->read_len;

      g_free (share_channel->read_buffer);
      share_channel->read_buffer = NULL;
      share_channel->read_len = 0;
    }
  while (len > 0)
    {
      guint consumed = http_data_received (self, share_channel, buffer, len);

      if (consumed == 0)
        {
          share_channel->read_buffer = g_memdup (buffer, len);
          share_channel->read_len = len;
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
set_session (GTalkFileCollection * self,
    GabbleJingleSession *session, GabbleJingleContent *content)
{
  self->priv->jingle = g_object_ref (session);

  gabble_signal_connect_weak (session, "notify::state",
      (GCallback) jingle_session_state_changed_cb, G_OBJECT (self));
  gabble_signal_connect_weak (session, "terminated",
      (GCallback) jingle_session_terminated_cb, G_OBJECT (self));

  gabble_signal_connect_weak (content, "new-share-channel",
      (GCallback) content_new_share_channel_cb, G_OBJECT (self));
  gabble_signal_connect_weak (content, "completed",
      (GCallback) content_completed, G_OBJECT (self));

  self->priv->status = GTALK_FT_STATUS_PENDING;
}

GTalkFileCollection *
gtalk_file_collection_new (GabbleFileTransferChannel *channel,
    GabbleJingleFactory *jingle_factory, TpHandle handle, const gchar *resource)
{
  GTalkFileCollection * self = g_object_new (GTALK_TYPE_FILE_COLLECTION, NULL);
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

  g_object_set (session,
      "dialect", JINGLE_DIALECT_GTALK4,
      NULL);

  content = gabble_jingle_session_add_content (session,
      JINGLE_MEDIA_TYPE_NONE, "share", NS_GOOGLE_SESSION_SHARE,
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

  set_session (self, session, content);

  add_channel (self, channel);


  return self;
}

GTalkFileCollection *
gtalk_file_collection_new_from_session (GabbleJingleFactory *jingle_factory,
    GabbleJingleSession *session)
{
  GTalkFileCollection * self = NULL;
  GabbleJingleContent *content = NULL;
  GList *cs;

  if (gabble_jingle_session_get_content_type (session) !=
      GABBLE_TYPE_JINGLE_SHARE)
      return NULL;

  cs = gabble_jingle_session_get_contents (session);

  if (cs != NULL)
    {
      content = GABBLE_JINGLE_CONTENT (cs->data);
      g_list_free (cs);
    }

  if (content == NULL)
    return NULL;

  self = g_object_new (GTALK_TYPE_FILE_COLLECTION, NULL);

  self->priv->jingle_factory = jingle_factory;
  self->priv->requested = FALSE;

  set_session (self, session, content);

  return self;
}

void
gtalk_file_collection_add_channel (GTalkFileCollection *self,
    GabbleFileTransferChannel *channel)
{
      add_channel (self, channel);
}

void
gtalk_file_collection_initiate (GTalkFileCollection *self,
    GabbleFileTransferChannel * channel)
{
  if (channel_exists (self, channel))
    {
      g_hash_table_replace (self->priv->channels_reading, channel,
          GINT_TO_POINTER (TRUE));
      g_hash_table_replace (self->priv->channels_usable, channel,
          GINT_TO_POINTER (TRUE));
    }

  if (self->priv->status == GTALK_FT_STATUS_PENDING)
    {
      gabble_jingle_session_accept (self->priv->jingle);
      self->priv->status = GTALK_FT_STATUS_INITIATED;
    }
  else
    {
      gabble_file_transfer_channel_gtalk_file_collection_state_changed (
          channel, GTALK_FILE_COLLECTION_STATE_ACCEPTED, FALSE);
    }

}

void
gtalk_file_collection_accept (GTalkFileCollection *self,
    GabbleFileTransferChannel * channel)
{
  GList *cs = gabble_jingle_session_get_contents (self->priv->jingle);

  DEBUG ("called");

  if (channel_exists (self, channel))
    {
      g_hash_table_replace (self->priv->channels_usable, channel,
          GINT_TO_POINTER (TRUE));
    }

  if (self->priv->status == GTALK_FT_STATUS_PENDING)
    {
      if (cs != NULL)
        {
          GabbleJingleContent *content = GABBLE_JINGLE_CONTENT (cs->data);
          guint initial_id = 0;
          guint share_channel_id;

          /* The new-share-channel signal will take care of the rest.. */
          do
            {
              gchar *share_channel_name = NULL;

              share_channel_name = g_strdup_printf ("gabble-%d", ++initial_id);
              share_channel_id = gabble_jingle_content_create_share_channel (
                  content, share_channel_name);
              g_free (share_channel_name);
            } while (share_channel_id == 0 && initial_id < 10);

          /* FIXME: not assert but actually cancel the FT? */
          g_assert (share_channel_id > 0);
          g_list_free (cs);
        }

      gabble_jingle_session_accept (self->priv->jingle);
      self->priv->status = GTALK_FT_STATUS_ACCEPTED;
    }
  else
    {
      gabble_file_transfer_channel_gtalk_file_collection_state_changed (
          channel, GTALK_FILE_COLLECTION_STATE_ACCEPTED, FALSE);
    }

  if (self->priv->status == GTALK_FT_STATUS_WAITING)
    {
      /* FIXME: this and other lookups should not check for channel '1' */
      ShareChannel *share_channel = g_hash_table_lookup (
          self->priv->share_channels, GINT_TO_POINTER (1));

      get_next_manifest_entry (self, share_channel, FALSE);
    }
}

gboolean
gtalk_file_collection_send_data (GTalkFileCollection *self,
    GabbleFileTransferChannel *channel, const gchar *data, guint length)
{

  ShareChannel *share_channel = g_hash_table_lookup (self->priv->share_channels,
      GINT_TO_POINTER (1));
  gint ret;


  g_return_val_if_fail (self->priv->current_channel == channel, FALSE);

  ret = nice_agent_send (share_channel->agent, share_channel->stream_id,
      share_channel->component_id, length, data);

  if (ret < 0 || (guint) ret < length)
    {
      if (ret < 0)
        ret = 0;

      share_channel->write_buffer = g_memdup (data + ret,
          length - ret);
      share_channel->write_len = length - ret;

      gabble_file_transfer_channel_gtalk_file_collection_write_blocked (channel,
          TRUE);
    }
  return TRUE;
}

void
gtalk_file_collection_block_reading (GTalkFileCollection *self,
    GabbleFileTransferChannel *channel, gboolean block)
{
  ShareChannel *share_channel = g_hash_table_lookup (self->priv->share_channels,
      GINT_TO_POINTER (1));

  g_assert (channel_exists (self, channel));

  if (self->priv->status != GTALK_FT_STATUS_TRANSFERRING)
    DEBUG ("Channel %p %s reading ", channel, block?"blocks":"unblocks" );

  g_hash_table_replace (self->priv->channels_reading, channel,
      GINT_TO_POINTER (!block));

  if (channel == self->priv->current_channel)
    {
      if (block)
        {
          if (share_channel && share_channel->agent_attached)
            {
              nice_agent_attach_recv (share_channel->agent,
                  share_channel->stream_id, share_channel->component_id,
                  NULL, NULL, NULL);
              share_channel->agent_attached = FALSE;
            }
        }
      else
        {
          if (share_channel && !share_channel->agent_attached)
            {
              share_channel->agent_attached = TRUE;
              nice_agent_attach_recv (share_channel->agent,
                  share_channel->stream_id, share_channel->component_id,
                  g_main_context_default (), nice_data_received_cb, self);
            }
        }
    }
}

void
gtalk_file_collection_completed (GTalkFileCollection *self,
    GabbleFileTransferChannel * channel)
{
  ShareChannel *share_channel = g_hash_table_lookup (self->priv->share_channels,
      GINT_TO_POINTER (1));

  DEBUG ("called");

  g_return_if_fail (self->priv->current_channel == channel);

  /* We shouldn't set the FT to completed until we receive the 'complete' info
     or we receive a new HTTP request otherwise we might terminate the session
     and cause a race condition where the peer thinks it got canceled before it
     completed. */
  share_channel->http_status = HTTP_SERVER_IDLE;
  self->priv->status = GTALK_FT_STATUS_WAITING;
}

void
gtalk_file_collection_terminate (GTalkFileCollection *self,
    GabbleFileTransferChannel * channel)
{

  DEBUG ("called");

  if (!channel_exists (self, channel))
    return;

  if (self->priv->current_channel == channel)
    {

      del_channel (self, channel);

      /* Cancel the whole thing if we terminate the current channel */
      if (self->priv->status == GTALK_FT_STATUS_TRANSFERRING)
        {

          /* The terminate should call our terminated_cb callback which should
             terminate all channels which should unref us which will unref the
             jingle session */
          self->priv->status = GTALK_FT_STATUS_TERMINATED;
          gabble_jingle_session_terminate (self->priv->jingle,
              TP_CHANNEL_GROUP_CHANGE_REASON_NONE, NULL, NULL);
          return;
        }
      return;
    }
  else
    {
      del_channel (self, channel);

      /* If this was the last channel, it will cause it to unref us and
         the dispose will be called, which will call
         gabble_jingle_session_terminate */
      gabble_file_transfer_channel_gtalk_file_collection_state_changed (channel,
          GTALK_FILE_COLLECTION_STATE_TERMINATED, TRUE);
    }
}


static void
channel_disposed (gpointer data, GObject *object)
{
  GTalkFileCollection *self = data;
  GabbleFileTransferChannel *channel = (GabbleFileTransferChannel *) object;

  DEBUG ("channel %p got destroyed", channel);

  g_return_if_fail (channel_exists (self, channel));

  if (self->priv->current_channel == channel)
    {
      del_channel (self, channel);

      /* Cancel the whole thing if we terminate the current channel */
      if (self->priv->status == GTALK_FT_STATUS_TRANSFERRING)
        {

          /* The terminate should call our terminated_cb callback which should
             terminate all channels which should unref us which will unref the
             jingle session */
          self->priv->status = GTALK_FT_STATUS_TERMINATED;
          gabble_jingle_session_terminate (self->priv->jingle,
              TP_CHANNEL_GROUP_CHANGE_REASON_NONE, NULL, NULL);
          return;
        }
    }
  else
    {
      del_channel (self, channel);
    }
}
