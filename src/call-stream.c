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


#include <stdio.h>
#include <stdlib.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/svc-properties-interface.h>
#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/gtypes.h>
#include <extensions/extensions.h>

#include "call-stream.h"
#include "call-stream-endpoint.h"
#include "connection.h"
#include "jingle-session.h"
#include "jingle-content.h"
#include "util.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"

static void call_stream_iface_init (gpointer, gpointer);
static void call_stream_media_iface_init (gpointer, gpointer);
static void call_stream_update_member_states (GabbleCallStream *self);

G_DEFINE_TYPE_WITH_CODE(GabbleCallStream, gabble_call_stream,
  G_TYPE_OBJECT,
   G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
    tp_dbus_properties_mixin_iface_init);
   G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CALL_STREAM,
    call_stream_iface_init);
   G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CALL_STREAM_INTERFACE_MEDIA,
    call_stream_media_iface_init);
  );

/* interfaces */
static const gchar *gabble_call_stream_interfaces[] = {
    GABBLE_IFACE_CALL_STREAM_INTERFACE_MEDIA,
    NULL
};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_JINGLE_CONTENT,
  PROP_CONNECTION,

  /* Call interface properties */
  PROP_INTERFACES,
  PROP_REMOTE_MEMBERS,
  PROP_CAN_REQUEST_RECEIVING,

  /* Media interface properties */
  PROP_LOCAL_CANDIDATES,
  PROP_ENDPOINTS,
  PROP_TRANSPORT,
  PROP_STUN_SERVERS,
  PROP_RELAY_INFO,
  PROP_RETRIEVED_SERVER_INFO,
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

  gchar *object_path;
  GabbleConnection *conn;
  GabbleJingleContent *content;

  GHashTable *remote_members;
  GList *endpoints;
  GPtrArray *relay_info;

  GabbleSendingState local_sending_state;

  gboolean got_relay_info;
};

static void
gabble_call_stream_init (GabbleCallStream *self)
{
  GabbleCallStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CALL_STREAM, GabbleCallStreamPrivate);

  self->priv = priv;
  priv->remote_members = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void gabble_call_stream_dispose (GObject *object);
static void gabble_call_stream_finalize (GObject *object);

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
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_JINGLE_CONTENT:
        g_value_set_object (value, priv->content);
        break;
      case PROP_LOCAL_CANDIDATES:
        {
          GPtrArray *arr;
          GList *candidates =
            gabble_jingle_content_get_local_candidates (priv->content);

          arr = gabble_call_candidates_to_array (candidates);
          g_value_take_boxed (value, arr);
          break;
        }

      case PROP_ENDPOINTS:
        {
          GPtrArray *arr = g_ptr_array_sized_new (1);
          GList *l;

          for (l = priv->endpoints; l != NULL; l = g_list_next (l))
            {
              GabbleCallStreamEndpoint *e =
                GABBLE_CALL_STREAM_ENDPOINT (l->data);
              g_ptr_array_add (arr,
                g_strdup (gabble_call_stream_endpoint_get_object_path (e)));
            }

          g_value_take_boxed (value, arr);
          break;
        }
      case PROP_TRANSPORT:
        {
          JingleTransportType transport;
          guint i;
          guint tptransport = G_MAXUINT;
          guint transport_mapping[][2] = {
              { JINGLE_TRANSPORT_GOOGLE_P2P,
                GABBLE_STREAM_TRANSPORT_TYPE_GTALK_P2P },
              { JINGLE_TRANSPORT_RAW_UDP,
                GABBLE_STREAM_TRANSPORT_TYPE_RAW_UDP },
              { JINGLE_TRANSPORT_ICE_UDP,
                 GABBLE_STREAM_TRANSPORT_TYPE_ICE },
          };

          transport = gabble_jingle_content_get_transport_type (priv->content);

          for (i = 0; i < G_N_ELEMENTS (transport_mapping); i++)
            {
              if (transport_mapping[i][0] == transport)
                {
                  tptransport = transport_mapping[i][1];
                  break;
                }
            }
          g_assert (tptransport < G_MAXUINT);
          g_value_set_uint (value, tptransport);

          break;
        }
      case PROP_INTERFACES:
        g_value_set_boxed (value, gabble_call_stream_interfaces);
        break;
      case PROP_REMOTE_MEMBERS:
        g_value_set_boxed (value, priv->remote_members);
        break;
      case PROP_CAN_REQUEST_RECEIVING:
        /* TODO: set to TRUE conditionally when RequestReceiving is implemented */
        g_value_set_boolean (value, FALSE);
        break;
      case PROP_STUN_SERVERS:
        {
          GPtrArray *arr;
          GabbleConnection *connection;
          gchar *stun_server;
          guint stun_port;

          arr = g_ptr_array_sized_new (1);

          g_object_get (priv->content,
              "connection", &connection,
              NULL);

          /* maybe one day we'll support multiple STUN servers */
          if (gabble_jingle_factory_get_stun_server (
                connection->jingle_factory, &stun_server, &stun_port))
            {
              GValueArray *va = tp_value_array_build (2,
                  G_TYPE_STRING, stun_server,
                  G_TYPE_UINT, stun_port,
                  G_TYPE_INVALID);

              g_free (stun_server);
              g_ptr_array_add (arr, va);
            }

          g_object_unref (connection);

          g_value_take_boxed (value, arr);
          break;
        }
      case PROP_RELAY_INFO:
        {
          if (priv->relay_info == NULL)
            {
              GPtrArray *relay_info = g_ptr_array_sized_new (0);
              g_value_set_boxed (value, relay_info);
              g_ptr_array_free (relay_info, TRUE);
            }
          else
            g_value_set_boxed (value, priv->relay_info);

          break;
        }
      case PROP_RETRIEVED_SERVER_INFO:
        {
          g_value_set_boolean (value, priv->got_relay_info);
          break;
        }
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
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        g_assert (priv->conn != NULL);
        break;
      case PROP_OBJECT_PATH:
        g_free (priv->object_path);
        priv->object_path = g_value_dup_string (value);
        break;
      case PROP_JINGLE_CONTENT:
        priv->content = g_value_dup_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
maybe_emit_server_info_retrieved (GabbleCallStream *self)
{
  if (self->priv->got_relay_info)
    gabble_svc_call_stream_interface_media_emit_server_info_retrieved (self);
}

static void
google_relay_session_cb (GPtrArray *relays,
                         gpointer user_data)
{
  GabbleCallStreamPrivate *priv = GABBLE_CALL_STREAM (user_data)->priv;

  priv->relay_info =
      g_boxed_copy (TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST, relays);

  if (!priv->got_relay_info)
    {
      priv->got_relay_info = TRUE;
      maybe_emit_server_info_retrieved (user_data);
    }
}

static void
content_state_changed_cb (GabbleJingleContent *content,
    GParamSpec *spec,
    gpointer user_data)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (user_data);

  call_stream_update_member_states (self);
}

static void
content_remote_members_changed_cb (GabbleJingleContent *content,
    GParamSpec *spec,
    gpointer user_data)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (user_data);

  call_stream_update_member_states (self);
}

static void
gabble_call_stream_constructed (GObject *obj)
{
  GabbleCallStreamPrivate *priv;
  TpDBusDaemon *bus;
  GabbleCallStreamEndpoint *endpoint;
  gchar *path;
  JingleTransportType transport;

  priv = GABBLE_CALL_STREAM (obj)->priv;

  priv->local_sending_state = GABBLE_SENDING_STATE_NONE;

  /* register object on the bus */
  DEBUG ("Registering %s", priv->object_path);
  bus = tp_base_connection_get_dbus_daemon ((TpBaseConnection *) priv->conn);
  tp_dbus_daemon_register_object (bus, priv->object_path, obj);

  /* Currently we'll only have one endpoint we know right away */
  path = g_strdup_printf ("%s/Endpoint", priv->object_path);
  endpoint = gabble_call_stream_endpoint_new (bus, path, priv->content);
  priv->endpoints = g_list_append (priv->endpoints, endpoint);
  g_free (path);

  transport = gabble_jingle_content_get_transport_type (priv->content);

  if (transport == JINGLE_TRANSPORT_GOOGLE_P2P)
    {
      DEBUG ("Attempting to create Google relay session");

      /* See if our server is Google, and if it is, ask them for a relay.
       * We ask for enough relays for 2 components (RTP and RTCP) since we
       * don't yet know whether there will be RTCP. */
      gabble_jingle_factory_create_google_relay_session (
          priv->conn->jingle_factory, 2, google_relay_session_cb, obj);
    }
  else
    {
      priv->got_relay_info = TRUE;
    }

  call_stream_update_member_states (GABBLE_CALL_STREAM (obj));
  gabble_signal_connect_weak (priv->content, "notify::state",
    G_CALLBACK (content_state_changed_cb), obj);
  gabble_signal_connect_weak (priv->content, "notify::senders",
    G_CALLBACK (content_remote_members_changed_cb), obj);

  if (G_OBJECT_CLASS (gabble_call_stream_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gabble_call_stream_parent_class)->constructed (obj);
}

static gboolean
call_stream_remote_member_update_state (GabbleCallStream *self,
    TpHandle contact,
    GabbleSendingState state)
{
  GabbleCallStreamPrivate *priv = self->priv;
  gpointer state_p = 0;
  gboolean exists;

  exists = g_hash_table_lookup_extended (priv->remote_members,
    GUINT_TO_POINTER (contact),
    NULL,
    &state_p);

  if (exists && GPOINTER_TO_UINT (state_p) == state)
    return FALSE;

  DEBUG ("Updating remote member %d state: %d => %d", contact,
    GPOINTER_TO_UINT (state_p), state);

  g_hash_table_insert (priv->remote_members,
    GUINT_TO_POINTER (contact),
    GUINT_TO_POINTER (state));

  return TRUE;
}

static void
call_stream_update_member_states (GabbleCallStream *self)
{
  GabbleCallStreamPrivate *priv = self->priv;
  gboolean created_by_us;
  JingleContentState state;
  GabbleSendingState local_state = 0;
  GabbleSendingState remote_state = 0;
  GHashTable *updates;

  g_object_get (priv->content, "state", &state, NULL);

  if (state == JINGLE_CONTENT_STATE_REMOVING)
    return;

  created_by_us = gabble_jingle_content_is_created_by_us (priv->content);
  updates = g_hash_table_new (g_direct_hash, g_direct_equal);

  DEBUG ("Created by us?: %d, State: %d", created_by_us, state);

  if (gabble_jingle_content_sending (priv->content))
    {
      if (state == JINGLE_CONTENT_STATE_ACKNOWLEDGED)
        local_state = GABBLE_SENDING_STATE_SENDING;
      else
        local_state = GABBLE_SENDING_STATE_PENDING_SEND;
    }

  if (gabble_jingle_content_receiving (priv->content))
    {
      if (created_by_us && state != JINGLE_CONTENT_STATE_ACKNOWLEDGED)
        remote_state = GABBLE_SENDING_STATE_PENDING_SEND;
      else
        remote_state = GABBLE_SENDING_STATE_SENDING;
    }

  if (priv->local_sending_state != local_state)
    {
      gabble_svc_call_stream_emit_local_sending_state_changed (
          GABBLE_SVC_CALL_STREAM (self), local_state);
    }
  priv->local_sending_state = local_state;

  if (call_stream_remote_member_update_state (self,
        priv->content->session->peer, remote_state))
    {
      g_hash_table_insert (updates,
        GUINT_TO_POINTER (priv->content->session->peer),
        GUINT_TO_POINTER (remote_state));
    }

  if (g_hash_table_size (updates) > 0)
    {
      GArray *empty = g_array_new (FALSE, TRUE, sizeof (TpHandle));

      gabble_svc_call_stream_emit_remote_members_changed (self,
        updates,
        empty);
      g_array_unref (empty);
    }

  g_hash_table_unref (updates);
}


static void
gabble_call_stream_class_init (GabbleCallStreamClass *gabble_call_stream_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_call_stream_class);
  GParamSpec *param_spec;
  static TpDBusPropertiesMixinPropImpl stream_props[] = {
    { "Interfaces", "interfaces", NULL },
    { "RemoteMembers", "remote-members", NULL },
    { "CanRequestReceiving", "can-request-receiving", NULL },
    { NULL }
  };
  static TpDBusPropertiesMixinPropImpl stream_media_props[] = {
    { "Transport", "transport", NULL },
    { "LocalCandidates", "local-candidates", NULL },
    { "STUNServers", "stun-servers", NULL },
    { "RelayInfo", "relay-info", NULL },
    { "RetrievedServerInfo", "retrieved-server-info", NULL },
    { "Endpoints", "endpoints", NULL },
    { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { GABBLE_IFACE_CALL_STREAM,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        stream_props,
      },
      { GABBLE_IFACE_CALL_STREAM_INTERFACE_MEDIA,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        stream_media_props,
      },
      { NULL }
  };

  g_type_class_add_private (gabble_call_stream_class,
    sizeof (GabbleCallStreamPrivate));

  object_class->set_property = gabble_call_stream_set_property;
  object_class->get_property = gabble_call_stream_get_property;

  object_class->dispose = gabble_call_stream_dispose;
  object_class->finalize = gabble_call_stream_finalize;
  object_class->constructed = gabble_call_stream_constructed;

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
      "The D-Bus object path used for this "
      "object on the bus.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Interfaces",
      "Stream interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES,
      param_spec);

  param_spec = g_param_spec_boxed ("remote-members", "Remote members",
      "Remote member map",
      GABBLE_HASH_TYPE_CONTACT_SENDING_STATE_MAP,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REMOTE_MEMBERS,
      param_spec);

  param_spec = g_param_spec_boolean ("can-request-receiving", "CanRequestReceiving",
      "If true, the user can request that a remote contact starts sending on"
      "this stream.",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CAN_REQUEST_RECEIVING,
      param_spec);

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this call stream",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object ("jingle-content", "Jingle Content",
      "The Jingle Content related to this content object",
      GABBLE_TYPE_JINGLE_CONTENT,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_JINGLE_CONTENT,
      param_spec);

  param_spec = g_param_spec_boxed ("local-candidates", "LocalCandidates",
      "List of local candidates",
      GABBLE_ARRAY_TYPE_CANDIDATE_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LOCAL_CANDIDATES,
      param_spec);

  param_spec = g_param_spec_boxed ("endpoints", "Endpoints",
      "The endpoints of this content",
      TP_ARRAY_TYPE_OBJECT_PATH_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ENDPOINTS,
      param_spec);

  param_spec = g_param_spec_uint ("transport", "Transport",
      "The transport of this stream",
      0, G_MAXUINT, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TRANSPORT,
      param_spec);

  param_spec = g_param_spec_boxed ("stun-servers", "STUNServers",
      "List of STUN servers",
      GABBLE_ARRAY_TYPE_SOCKET_ADDRESS_IP_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STUN_SERVERS,
      param_spec);

  param_spec = g_param_spec_boxed ("relay-info", "RelayInfo",
      "List of relay information",
      TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_RELAY_INFO,
      param_spec);

  param_spec = g_param_spec_boolean ("retrieved-server-info",
      "RetrievedServerInfo",
      "True if the server information about STUN and "
      "relay servers has been retrieved",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_RETRIEVED_SERVER_INFO,
      param_spec);

  gabble_call_stream_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleCallStreamClass, dbus_props_class));
}

void
gabble_call_stream_dispose (GObject *object)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (object);
  GabbleCallStreamPrivate *priv = self->priv;
  GList *l;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  for (l = priv->endpoints; l != NULL; l = g_list_next (l))
    {
      g_object_unref (l->data);
    }

  tp_clear_pointer (&priv->endpoints, g_list_free);

  tp_clear_object (&priv->content);

  priv->conn = NULL;

  if (G_OBJECT_CLASS (gabble_call_stream_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_stream_parent_class)->dispose (object);
}

void
gabble_call_stream_finalize (GObject *object)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (object);
  GabbleCallStreamPrivate *priv = self->priv;

  /* free any data held directly by the object here */
  g_free (priv->object_path);

  g_hash_table_destroy (priv->remote_members);

  if (priv->relay_info != NULL)
    g_boxed_free (TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST, priv->relay_info);

  G_OBJECT_CLASS (gabble_call_stream_parent_class)->finalize (object);
}

static void
gabble_call_stream_add_candidates (GabbleSvcCallStreamInterfaceMedia *iface,
    const GPtrArray *candidates,
    DBusGMethodInvocation *context)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (iface);
  GabbleCallStreamPrivate *priv = self->priv;
  GList *l = NULL;
  guint i;

  for (i = 0; i < candidates->len ; i++)
    {
      GValueArray *va;
      JingleCandidate *c;
      GHashTable *info;
      guint fstype, type;

      va = g_ptr_array_index (candidates, i);

      info = g_value_get_boxed (va->values + 3);

      fstype = tp_asv_get_uint32 (info, "Type", NULL);

      switch (fstype)
        {
        case 0: /* FS_CANDIDATE_TYPE_HOST */
          type = JINGLE_CANDIDATE_TYPE_LOCAL;
          break;
        case 1: /* FS_CANDIDATE_TYPE_SRFLX */
        case 2: /* FS_CANDIDATE_TYPE_PRFLX */
          type = JINGLE_CANDIDATE_TYPE_STUN;
          break;
        case 3: /* FS_CANDIDATE_TYPE_RELAY */
          type = JINGLE_CANDIDATE_TYPE_RELAY;
          break;
        case 4: /* FS_CANDIDATE_TYPE_MULTICAST */
        default:
          DEBUG ("Unhandled candidate type %d", fstype);
          continue;
        }

      c = jingle_candidate_new (
        /* transport protocol */
        tp_asv_get_uint32 (info, "Protocol", NULL),
        /* Candidate type */
        type,
        /* id/foundation */
        tp_asv_get_string (info, "Foundation"),
        /* component */
        g_value_get_uint (va->values + 0),
        /* ip */
        g_value_get_string (va->values + 1),
        /* port */
        g_value_get_uint (va->values + 2),
        /* generation */
        0,
        /* preference */
        tp_asv_get_uint32 (info, "Priority", NULL) / 65536.0,
        /* username, password */
        tp_asv_get_string (info, "Username"),
        tp_asv_get_string (info, "Password"),
        /* network */
        0);

      l = g_list_append (l, c);
    }

  gabble_jingle_content_add_candidates (priv->content, l);

  gabble_svc_call_stream_interface_media_emit_local_candidates_added (self,
      candidates);

  gabble_svc_call_stream_interface_media_return_from_add_candidates (context);
}

static void
gabble_call_stream_candidates_prepared (
    GabbleSvcCallStreamInterfaceMedia *iface,
    DBusGMethodInvocation *context)
{
  gabble_svc_call_stream_interface_media_return_from_candidates_prepared (
    context);
}

void
gabble_call_stream_set_sending (GabbleCallStream *self,
    gboolean sending)
{
  GabbleCallStreamPrivate *priv = self->priv;
  GabbleSendingState orig_state, new_state;

  orig_state = priv->local_sending_state;

  if (sending)
    new_state = GABBLE_SENDING_STATE_SENDING;
  else
    new_state = GABBLE_SENDING_STATE_NONE;

  if (orig_state != new_state)
    {
      priv->local_sending_state = new_state;

      gabble_svc_call_stream_emit_local_sending_state_changed (
          GABBLE_SVC_CALL_STREAM (self), new_state);

      if (sending == (orig_state == GABBLE_SENDING_STATE_NONE))
        gabble_jingle_content_set_sending (priv->content, sending);
    }
}

static void
gabble_call_stream_set_sending_async (GabbleSvcCallStream *iface,
    gboolean sending,
    DBusGMethodInvocation *context)
{
  GabbleCallStream *self = GABBLE_CALL_STREAM (iface);

  gabble_call_stream_set_sending (self, sending);

  gabble_svc_call_stream_return_from_set_sending (context);
}

static void
call_stream_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleSvcCallStreamClass *klass =
    (GabbleSvcCallStreamClass *) g_iface;

#define IMPLEMENT(x, suffix) gabble_svc_call_stream_implement_##x (\
    klass, gabble_call_stream_##x##suffix)
  IMPLEMENT(set_sending, _async);
#undef IMPLEMENT
}

static void
call_stream_media_iface_init (gpointer g_iface, gpointer iface_data)
{
  GabbleSvcCallStreamInterfaceMediaClass *klass =
    (GabbleSvcCallStreamInterfaceMediaClass *) g_iface;

#define IMPLEMENT(x) gabble_svc_call_stream_interface_media_implement_##x (\
    klass, gabble_call_stream_##x)
  IMPLEMENT(add_candidates);
  IMPLEMENT(candidates_prepared);
#undef IMPLEMENT
}

const gchar *
gabble_call_stream_get_object_path (GabbleCallStream *stream)
{
  return stream->priv->object_path;
}

guint
gabble_call_stream_get_local_sending_state (GabbleCallStream *self)
{
  GabbleCallStreamPrivate *priv = self->priv;

  return priv->local_sending_state;
}

GabbleJingleContent *
gabble_call_stream_get_jingle_content (GabbleCallStream *stream)
{
  return stream->priv->content;
}
