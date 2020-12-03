/*
 * gabble-call-stream.c - Source for GabbleCallStream
 * Copyright (C) 2009 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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

#include <stdio.h>
#include <stdlib.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "call-stream.h"
#include "connection.h"
#include "jingle-tp-util.h"
#include "util.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"

static GPtrArray *gabble_call_stream_add_candidates (
    TpBaseMediaCallStream *stream,
    const GPtrArray *candidates,
    GError **error);
static gboolean gabble_call_stream_set_sending (TpBaseMediaCallStream *stream,
    gboolean sending,
    GError **error);
static void gabble_call_stream_request_receiving (TpBaseMediaCallStream *stream,
    TpHandle contact, gboolean receive);

G_DEFINE_TYPE(GabbleCallStream, gabble_call_stream,
    TP_TYPE_BASE_MEDIA_CALL_STREAM)

/* properties */
enum
{
  PROP_JINGLE_CONTENT = 1,

  /* Media interface properties */
  PROP_LOCAL_CANDIDATES,
  PROP_ENDPOINTS,
  PROP_TRANSPORT,
  PROP_STUN_SERVERS,
  PROP_RELAY_INFO,
  PROP_HAS_SERVER_INFO,
  PROP_CAN_REQUEST_RECEIVING
};

#if 0
/* signal enum */
enum
{
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
#endif


/* private structure */
struct _GabbleCallStreamPrivate
{
  gboolean dispose_has_run;

  WockyJingleContent *content;
};

static void
gabble_call_stream_init (GabbleCallStream *self)
{
  GabbleCallStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CALL_STREAM, GabbleCallStreamPrivate);

  self->priv = priv;
}

static void gabble_call_stream_dispose (GObject *object);
static void gabble_call_stream_finalize (GObject *object);

static GPtrArray *
get_stun_servers (GabbleCallStream *self)
{
  GPtrArray *arr;
  WockyJingleFactory *jf;
  GList *stun_servers;

  arr = g_ptr_array_new_with_free_func ((GDestroyNotify) tp_value_array_free);
  jf = wocky_jingle_session_get_factory (self->priv->content->session);
  stun_servers = wocky_jingle_info_get_stun_servers (
      wocky_jingle_factory_get_jingle_info (jf));

  while (stun_servers != NULL)
    {
      WockyStunServer *stun_server = stun_servers->data;
      GValueArray *va = tp_value_array_build (2,
          G_TYPE_STRING, stun_server->address,
          G_TYPE_UINT, (guint) stun_server->port,
          G_TYPE_INVALID);

      g_ptr_array_add (arr, va);

      stun_servers = g_list_delete_link (stun_servers, stun_servers);
    }

  return arr;
}

static void
gabble_call_stream_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  GabbleCallStream *stream = GABBLE_CALL_STREAM (object);
  GabbleCallStreamPrivate *priv = stream->priv;

  switch (property_id)
    {
      case PROP_JINGLE_CONTENT:
        g_value_set_object (value, priv->content);
        break;
      case PROP_CAN_REQUEST_RECEIVING:
        {
          WockyJingleDialect dialect =
              wocky_jingle_session_get_dialect (priv->content->session);
          g_value_set_boolean (value, !WOCKY_JINGLE_DIALECT_IS_GOOGLE (dialect));
        }
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_call_stream_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleCallStream *stream = GABBLE_CALL_STREAM (object);
  GabbleCallStreamPrivate *priv = stream->priv;

  switch (property_id)
    {
      case PROP_JINGLE_CONTENT:
        priv->content = g_value_dup_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
google_relay_session_cb (GPtrArray *relays,
                         gpointer user_data)
{
  TpWeakRef *weak_ref = user_data;
  TpBaseMediaCallStream *stream = TP_BASE_MEDIA_CALL_STREAM (
      tp_weak_ref_dup_object (weak_ref));

  if (stream != NULL)
    {
      GPtrArray *tp_relays = gabble_build_tp_relay_info (relays);

      tp_base_media_call_stream_set_relay_info (stream, tp_relays);
      g_ptr_array_unref (tp_relays);
      g_object_unref (stream);
    }

  tp_weak_ref_destroy (weak_ref);
}

static void
content_state_changed_cb (WockyJingleContent *content,
    GParamSpec *spec,
    gpointer user_data)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (user_data);

  gabble_call_stream_update_member_states (self);
}

static void
content_remote_members_changed_cb (WockyJingleContent *content,
    GParamSpec *spec,
    gpointer user_data)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (user_data);

  gabble_call_stream_update_member_states (self);
}

static void
jingle_info_stun_server_changed_cb (
    WockyJingleInfo *jingle_info,
    const gchar *stun_server,
    guint stun_port,
    GabbleCallStream *self)
{
  GPtrArray *stun_servers = get_stun_servers (self);

  tp_base_media_call_stream_set_stun_servers (
    TP_BASE_MEDIA_CALL_STREAM (self), stun_servers);
  g_ptr_array_unref (stun_servers);
}


static void
_new_candidates_cb (
    WockyJingleContent *content,
    GList *candidates,
    TpCallStreamEndpoint *endpoint)
{
  GPtrArray *tp_candidates;
  gchar *ufrag, *pwd;

  if (candidates == NULL)
    return;

  if (wocky_jingle_content_get_credentials (content, &ufrag, &pwd))
    tp_call_stream_endpoint_set_remote_credentials (endpoint, ufrag, pwd);

  tp_candidates = gabble_call_candidates_to_array (candidates);
  tp_call_stream_endpoint_add_new_candidates (endpoint, tp_candidates);
  g_boxed_free (TP_ARRAY_TYPE_CANDIDATE_LIST, tp_candidates);
}

static void
_endpoint_state_changed_cb (
    TpCallStreamEndpoint *endpoint,
    GParamSpec *spec,
    WockyJingleContent *content)
{
  TpStreamEndpointState state;

  /* We only care about connecting RTP, RTCP is optional */
  state = tp_call_stream_endpoint_get_state (endpoint, 1);
  wocky_jingle_content_set_transport_state (content, (WockyJingleTransportState) state);
}

static TpCallStreamEndpoint *
_hook_up_endpoint (GabbleCallStream *self,
    const gchar *path,
    WockyJingleContent *content)
{
  TpBaseCallStream *base = (TpBaseCallStream *) self;
  TpBaseConnection *conn = tp_base_call_stream_get_connection (base);
  TpDBusDaemon *bus = tp_base_connection_get_dbus_daemon (conn);
  TpCallStreamEndpoint *endpoint;
  TpStreamTransportType type = 0;
  GPtrArray *tp_candidates;
  GList *candidates;
  gchar *ufrag, *pwd;

  switch (wocky_jingle_content_get_transport_type (content))
    {
    case JINGLE_TRANSPORT_GOOGLE_P2P:
      type = TP_STREAM_TRANSPORT_TYPE_GTALK_P2P;
      break;
    case JINGLE_TRANSPORT_RAW_UDP:
      type = TP_STREAM_TRANSPORT_TYPE_RAW_UDP;
      break;
    case JINGLE_TRANSPORT_ICE_UDP:
      type = TP_STREAM_TRANSPORT_TYPE_ICE;
      break;
    case JINGLE_TRANSPORT_UNKNOWN:
    default:
      g_assert_not_reached ();
    }

  /* FIXME: ice??? */
  endpoint = tp_call_stream_endpoint_new (bus, path, type, FALSE);

  if (wocky_jingle_content_get_credentials (content, &ufrag, &pwd))
    tp_call_stream_endpoint_set_remote_credentials (endpoint, ufrag, pwd);
  candidates = wocky_jingle_content_get_remote_candidates (content);
  tp_candidates = gabble_call_candidates_to_array (candidates);
  tp_call_stream_endpoint_add_new_candidates (endpoint, tp_candidates);
  g_boxed_free (TP_ARRAY_TYPE_CANDIDATE_LIST, tp_candidates);

  tp_g_signal_connect_object (content, "new-candidates",
      G_CALLBACK (_new_candidates_cb), endpoint, 0);

  tp_g_signal_connect_object (endpoint, "notify::endpoint-state",
      G_CALLBACK(_endpoint_state_changed_cb), content, 0);

  return endpoint;
}

static void
gabble_call_stream_constructed (GObject *obj)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (obj);
  TpBaseCallStream *base = (TpBaseCallStream *) self;
  TpBaseMediaCallStream *media_base = (TpBaseMediaCallStream *) self;
  GabbleCallStreamPrivate *priv = self->priv;
  GabbleConnection *conn;
  TpCallStreamEndpoint *endpoint;
  gchar *path;
  WockyJingleTransportType transport;
  GPtrArray *stun_servers;
  gboolean locally_created;

  if (G_OBJECT_CLASS (gabble_call_stream_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gabble_call_stream_parent_class)->constructed (obj);

  conn = GABBLE_CONNECTION (tp_base_call_stream_get_connection (base));

  g_object_get (priv->content, "locally-created", &locally_created, NULL);

  if (locally_created &&
      wocky_jingle_content_sending (priv->content))
    tp_base_media_call_stream_set_local_sending (
        TP_BASE_MEDIA_CALL_STREAM (self), TRUE);


  /* Currently we'll only have one endpoint we know right away */
  path = g_strdup_printf ("%s/Endpoint",
      tp_base_call_stream_get_object_path (base));
  endpoint = _hook_up_endpoint (self, path, priv->content);
  tp_base_media_call_stream_add_endpoint (media_base, endpoint);
  g_object_unref (endpoint);
  g_free (path);

  transport = wocky_jingle_content_get_transport_type (priv->content);

  if (transport == JINGLE_TRANSPORT_GOOGLE_P2P)
    {
      DEBUG ("Attempting to create Google relay session");

      /* See if our server is Google, and if it is, ask them for a relay.
       * We ask for enough relays for 2 components (RTP and RTCP) since we
       * don't yet know whether there will be RTCP. */
      wocky_jingle_info_create_google_relay_session (
          gabble_jingle_mint_get_info (conn->jingle_mint),
          2, google_relay_session_cb, tp_weak_ref_new (self, NULL, NULL));
    }
  else
    {
      GPtrArray *relays = g_ptr_array_new ();
      tp_base_media_call_stream_set_relay_info (media_base, relays);
      g_ptr_array_unref (relays);
    }

  stun_servers = get_stun_servers (self);
  tp_base_media_call_stream_set_stun_servers (
    TP_BASE_MEDIA_CALL_STREAM (self), stun_servers);
  g_ptr_array_unref (stun_servers);

  gabble_call_stream_update_member_states (GABBLE_CALL_STREAM (obj));
  gabble_signal_connect_weak (priv->content, "notify::state",
    G_CALLBACK (content_state_changed_cb), obj);
  gabble_signal_connect_weak (priv->content, "notify::senders",
    G_CALLBACK (content_remote_members_changed_cb), obj);
  gabble_signal_connect_weak (
      gabble_jingle_mint_get_info (conn->jingle_mint),
      "stun-server-changed",
      G_CALLBACK (jingle_info_stun_server_changed_cb), obj);
}

void
gabble_call_stream_update_member_states (GabbleCallStream *self)
{
  TpBaseCallStream *base = TP_BASE_CALL_STREAM (self);
  TpBaseMediaCallStream *bmcs = TP_BASE_MEDIA_CALL_STREAM (self);
  GabbleCallStreamPrivate *priv = self->priv;
  WockyJingleContentState state;
  TpSendingState local_state;
  TpSendingState remote_state;
  TpBaseConnection *conn = tp_base_call_stream_get_connection (base);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle peer;

  g_object_get (priv->content, "state", &state, NULL);

  if (state == WOCKY_JINGLE_CONTENT_STATE_REMOVING)
    return;

  if (wocky_jingle_content_sending (priv->content))
    {
      if (tp_base_media_call_stream_get_local_sending (bmcs))
        local_state = TP_SENDING_STATE_SENDING;
      else
        local_state = TP_SENDING_STATE_PENDING_SEND;
    }
  else
    {
      if (tp_base_media_call_stream_get_local_sending (bmcs))
        local_state = TP_SENDING_STATE_PENDING_STOP_SENDING;
      else
        local_state = TP_SENDING_STATE_NONE;
    }

  if (wocky_jingle_content_receiving (priv->content))
    {
      remote_state = TP_SENDING_STATE_SENDING;
    }
  else
    {
      remote_state = TP_SENDING_STATE_NONE;
    }

  DEBUG ("State: %d Local: %d Remote: %d", state, local_state, remote_state);

  tp_base_call_stream_update_local_sending_state (base, local_state,
      0, TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE, "", "");
  peer = tp_handle_ensure (contact_repo,
      wocky_jingle_session_get_peer_jid (priv->content->session),
      NULL,
      NULL);
  tp_base_call_stream_update_remote_sending_state (base,
        peer, remote_state,
        0, TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE, "", "");
}


static void
gabble_call_stream_class_init (GabbleCallStreamClass *gabble_call_stream_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_call_stream_class);
  TpBaseMediaCallStreamClass *bmcs_class =
      TP_BASE_MEDIA_CALL_STREAM_CLASS (gabble_call_stream_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_call_stream_class,
    sizeof (GabbleCallStreamPrivate));

  object_class->set_property = gabble_call_stream_set_property;
  object_class->get_property = gabble_call_stream_get_property;

  object_class->dispose = gabble_call_stream_dispose;
  object_class->finalize = gabble_call_stream_finalize;
  object_class->constructed = gabble_call_stream_constructed;

  param_spec = g_param_spec_object ("jingle-content", "Jingle Content",
      "The Jingle Content related to this content object",
      WOCKY_TYPE_JINGLE_CONTENT,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_JINGLE_CONTENT,
      param_spec);

  g_object_class_override_property (object_class, PROP_CAN_REQUEST_RECEIVING,
      "can-request-receiving");

  bmcs_class->add_local_candidates = gabble_call_stream_add_candidates;
  bmcs_class->set_sending = gabble_call_stream_set_sending;
  bmcs_class->request_receiving = gabble_call_stream_request_receiving;
}

void
gabble_call_stream_dispose (GObject *object)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (object);
  GabbleCallStreamPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_clear_object (&priv->content);

  if (G_OBJECT_CLASS (gabble_call_stream_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_stream_parent_class)->dispose (object);
}

void
gabble_call_stream_finalize (GObject *object)
{
  G_OBJECT_CLASS (gabble_call_stream_parent_class)->finalize (object);
}

static GPtrArray *
gabble_call_stream_add_candidates (TpBaseMediaCallStream *stream,
    const GPtrArray *candidates,
    GError **error)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (stream);
  GabbleCallStreamPrivate *priv = self->priv;
  GPtrArray *accepted_candidates = g_ptr_array_sized_new (candidates->len);
  GList *l = NULL;
  guint i;

  for (i = 0; i < candidates->len ; i++)
    {
      GValueArray *va;
      WockyJingleCandidate *c;
      GHashTable *info;
      guint tptype;
      WockyJingleCandidateType type;
      /* borrowed strings, owned by other people. */
      const gchar *username;
      const gchar *password;
      const gchar *foundation;

      va = g_ptr_array_index (candidates, i);

      info = g_value_get_boxed (va->values + 3);

      tptype = tp_asv_get_uint32 (info, "type", NULL);
      switch (tptype)
        {
        default:
          /* Anything else is local */
        case TP_CALL_STREAM_CANDIDATE_TYPE_HOST:
          type = WOCKY_JINGLE_CANDIDATE_TYPE_LOCAL;
          break;
        case TP_CALL_STREAM_CANDIDATE_TYPE_SERVER_REFLEXIVE:
          type = WOCKY_JINGLE_CANDIDATE_TYPE_STUN;
          break;
        case TP_CALL_STREAM_CANDIDATE_TYPE_RELAY:
          type = WOCKY_JINGLE_CANDIDATE_TYPE_RELAY;
          break;
        }

      username = tp_asv_get_string (info, "username");
      if (username == NULL)
        username = tp_base_media_call_stream_get_username (stream);

      password = tp_asv_get_string (info, "password");
      if (password == NULL)
        password = tp_base_media_call_stream_get_password (stream);

      foundation = tp_asv_get_string (info, "foundation");
      if (foundation == NULL)
        foundation = "1";

      c = wocky_jingle_candidate_new (
        /* transport protocol */
        tp_asv_get_uint32 (info, "protocol", NULL),
        /* Candidate type */
        type,
        /* id/foundation */
        foundation,
        /* component */
        g_value_get_uint (va->values + 0),
        /* ip */
        g_value_get_string (va->values + 1),
        /* port */
        g_value_get_uint (va->values + 2),
        /* generation */
        0,
        /* preference */
        tp_asv_get_uint32 (info, "priority", NULL),
        /* username, password */
        username,
        password,
        /* network */
        0);

      l = g_list_append (l, c);
      g_ptr_array_add (accepted_candidates, va);
    }

  wocky_jingle_content_add_candidates (priv->content, l);

  if (accepted_candidates->len == 0 && candidates->len != 0)
    {
      g_set_error_literal (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "All candidates had the wrong Type");
      tp_clear_pointer (&accepted_candidates, g_ptr_array_unref);
    }

  return accepted_candidates;
}

static gboolean
gabble_call_stream_set_sending (TpBaseMediaCallStream *stream,
    gboolean sending,
    GError **error)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (stream);

  if (sending)
    tp_base_media_call_stream_set_local_sending (stream, TRUE);

  wocky_jingle_content_set_sending (self->priv->content, sending);

  return TRUE;
}

static void gabble_call_stream_request_receiving (TpBaseMediaCallStream *stream,
    TpHandle contact, gboolean receive)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (stream);

  wocky_jingle_content_request_receiving (self->priv->content, receive);
}

WockyJingleContent *
gabble_call_stream_get_jingle_content (GabbleCallStream *stream)
{
  return stream->priv->content;
}
