/*
 * call-muc-channel.c - Source for CallMucChannel
 * Copyright (C) 2010 Collabora Ltd.
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

#include <telepathy-glib/interfaces.h>
#include <wocky/wocky-muc.h>

#include "muc-channel.h"
#include "call-muc-channel.h"
#include "util.h"
#include "namespaces.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"

static void async_initable_iface_init (GAsyncInitableIface *iface);

static void call_muc_channel_accept (GabbleBaseCallChannel *channel);

G_DEFINE_TYPE_WITH_CODE (GabbleCallMucChannel,
  gabble_call_muc_channel, GABBLE_TYPE_BASE_CALL_CHANNEL,
  G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init);
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
    tp_external_group_mixin_iface_init));

/* signal enum */
#if 0
enum
{
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
#endif

enum
{
  PROP_MUC = 1,
};

/* private structure */
struct _GabbleCallMucChannelPrivate
{
  gboolean dispose_has_run;

  GabbleMucChannel *muc;
  WockyMuc *wmuc;
  gboolean initialized;
};

#define GABBLE_CALL_MUC_CHANNEL_GET_PRIVATE(o)   \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
  GABBLE_TYPE_CALL_MUC_CHANNEL, GabbleCallMucChannelPrivate))

static void
gabble_call_muc_channel_init (GabbleCallMucChannel *self)
{
  GabbleCallMucChannelPrivate *priv =
    GABBLE_CALL_MUC_CHANNEL_GET_PRIVATE (self);

  self->priv = priv;
}

static void
gabble_call_muc_channel_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (object);
  GabbleCallMucChannelPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_MUC:
        g_value_set_object (value, priv->muc);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_call_muc_channel_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (object);
  GabbleCallMucChannelPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_MUC:
        priv->muc = g_value_get_object (value);
        g_assert (priv->muc != NULL);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}


static void gabble_call_muc_channel_dispose (GObject *object);
static void gabble_call_muc_channel_finalize (GObject *object);

static void
gabble_call_muc_channel_constructed (GObject *obj)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (obj);

  tp_external_group_mixin_init (obj, G_OBJECT (self->priv->muc));

  if (G_OBJECT_CLASS (gabble_call_muc_channel_parent_class)->constructed
      != NULL)
    G_OBJECT_CLASS (gabble_call_muc_channel_parent_class)->constructed (obj);
}

static void
gabble_call_muc_channel_class_init (
    GabbleCallMucChannelClass *gabble_call_muc_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_call_muc_channel_class);
  GabbleBaseCallChannelClass *base_call_class =
    GABBLE_BASE_CALL_CHANNEL_CLASS (gabble_call_muc_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_call_muc_channel_class,
    sizeof (GabbleCallMucChannelPrivate));

  object_class->set_property = gabble_call_muc_channel_set_property;
  object_class->get_property = gabble_call_muc_channel_get_property;

  object_class->constructed = gabble_call_muc_channel_constructed;
  object_class->dispose = gabble_call_muc_channel_dispose;
  object_class->finalize = gabble_call_muc_channel_finalize;

  base_call_class->handle_type = TP_HANDLE_TYPE_ROOM;
  base_call_class->accept = call_muc_channel_accept;

  param_spec = g_param_spec_object ("muc", "GabbleMuc object",
      "The muc to which this call is related",
      GABBLE_TYPE_MUC_CHANNEL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MUC, param_spec);

  tp_external_group_mixin_init_dbus_properties (object_class);
}

void
gabble_call_muc_channel_dispose (GObject *object)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (object);
  GabbleCallMucChannelPrivate *priv =
    GABBLE_CALL_MUC_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->wmuc != NULL)
    g_object_unref (priv->wmuc);
  priv->wmuc = NULL;

  tp_external_group_mixin_finalize (object);

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_call_muc_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_muc_channel_parent_class)->dispose (object);
}

void
gabble_call_muc_channel_finalize (GObject *object)
{
  /* free any data held directly by the object here */

  G_OBJECT_CLASS (gabble_call_muc_channel_parent_class)->finalize (object);
}

static void
call_muc_channel_content_local_codecs_updated (GabbleCallContent *content,
    GList *local_codecs,
    gpointer user_data)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (user_data);

  DEBUG ("Local codecs of a content updated, forcing presence sent");
  gabble_muc_channel_send_presence (self->priv->muc, NULL);
}

static void
call_muc_channel_setup_content (GabbleCallMucChannel *self,
    GabbleCallContent *content)
{
  DEBUG ("Setting up content");
  gabble_signal_connect_weak (content, "local-codecs-updated",
    G_CALLBACK (call_muc_channel_content_local_codecs_updated),
    G_OBJECT (self));
}

static void
call_muc_channel_member_content_added_cb (GabbleCallMember *member,
    GabbleCallMemberContent *content,
    gpointer user_data)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (user_data);
  const gchar *name;
  JingleMediaType mtype;
  GList *l;
  GabbleCallContent *ccontent;

  /* A new content was added for one of the members, match it up with the call
   * channels contents */
  name = gabble_call_member_content_get_name (content);
  mtype = gabble_call_member_content_get_media_type (content);

  DEBUG ("New call member content: %s (type: %d)", name, mtype);

  for (l = gabble_base_call_channel_get_contents (
      GABBLE_BASE_CALL_CHANNEL (self)); l != NULL; l = g_list_next (l))
    {
      const char *cname;
      JingleMediaType cmtype;

      ccontent = GABBLE_CALL_CONTENT (l->data);
      cname = gabble_call_content_get_name (ccontent);
      cmtype = gabble_call_content_get_media_type (ccontent);

      if (!tp_strdiff (cname, name) && mtype == cmtype)
        goto have_content;
    }

  ccontent = gabble_base_call_channel_add_content (
      GABBLE_BASE_CALL_CHANNEL (self), name, mtype,
      self->priv->initialized ? GABBLE_CALL_CONTENT_DISPOSITION_INITIAL : 0);
  call_muc_channel_setup_content (self, ccontent);

have_content:
  gabble_call_content_add_member_content (ccontent, content);
}

static GList *
call_muc_channel_parse_codecs (GabbleCallMucChannel *self,
    WockyXmppNode *description)
{
  GList *codecs = NULL;
  WockyXmppNodeIter iter;
  WockyXmppNode *payload;

  wocky_xmpp_node_iter_init (&iter, description,
      "payload-type", NS_JINGLE_RTP);
  while (wocky_xmpp_node_iter_next (&iter, &payload))
    {
      const gchar *name;
      const gchar *value;
      guint id;
      guint clockrate = 0;
      guint channels = 0;
      JingleCodec *codec;
      WockyXmppNodeIter param_iter;
      WockyXmppNode *parameter;

      value = wocky_xmpp_node_get_attribute (payload, "id");
      if (value == NULL)
        continue;
      id = atoi (value);

      name = wocky_xmpp_node_get_attribute (payload, "name");
      if (name == NULL)
        continue;

      value = wocky_xmpp_node_get_attribute (payload, "clockrate");
      if (value != NULL)
        clockrate = atoi (value);

      value = wocky_xmpp_node_get_attribute (payload, "channels");
      if (value != NULL)
        channels = atoi (value);

      codec = jingle_media_rtp_codec_new (id, name, clockrate, channels, NULL);

      codecs = g_list_append (codecs, codec);

      wocky_xmpp_node_iter_init (&param_iter, payload,
        "parameter", NS_JINGLE_RTP);
      while (wocky_xmpp_node_iter_next (&param_iter, &parameter))
        {
          const gchar *key;

          key = wocky_xmpp_node_get_attribute (parameter, "name");
          value = wocky_xmpp_node_get_attribute (parameter, "value");

          if (key == NULL || value == NULL)
            continue;

          g_hash_table_insert (codec->params,
              g_strdup (key), g_strdup (value));
        }
    }

  return codecs;
}

static void
call_muc_channel_got_participant_presence (GabbleCallMucChannel *self,
  WockyMucMember *member,
  WockyXmppStanza *stanza)
{
  GabbleCallMucChannelPrivate *priv = self->priv;
  GabbleCallMember *call_member;
  TpHandle handle;
  TpHandleRepoIface *contact_repo =
    tp_base_connection_get_handles (
      (TpBaseConnection *) GABBLE_BASE_CALL_CHANNEL (self)->conn,
        TP_HANDLE_TYPE_CONTACT);
  WockyXmppNode *muji;
  WockyXmppNodeIter iter;
  WockyXmppNode *content;

  muji = wocky_xmpp_node_get_child_ns (stanza->node, "muji", NS_MUJI);

  if (muji == NULL)
    return;

  DEBUG ("Muji participant: %s", member->from);

  handle = tp_handle_ensure (contact_repo, member->from, NULL, NULL);

  call_member = gabble_base_call_channel_get_member_from_handle (
    GABBLE_BASE_CALL_CHANNEL (self), handle);

  if (call_member == NULL)
    {
      call_member = gabble_base_call_channel_ensure_member_from_handle (
        GABBLE_BASE_CALL_CHANNEL (self), handle);
      gabble_signal_connect_weak (call_member, "content-added",
        G_CALLBACK (call_muc_channel_member_content_added_cb),
        G_OBJECT (self));
      gabble_call_member_accept (call_member);
    }

  wocky_xmpp_node_iter_init (&iter, muji, "content", NS_MUJI);
  while (wocky_xmpp_node_iter_next (&iter, &content))
    {
      GabbleCallMemberContent *member_content;
      WockyXmppNode *description;
      JingleMediaType mtype;
      const gchar *name;
      const gchar *mattr;
      GList *codecs;

      name = wocky_xmpp_node_get_attribute (content, "name");
      if (name == NULL)
        {
          DEBUG ("Content is missing the name attribute");
          continue;
        }

      DEBUG ("Parsing content: %s", name);

      description = wocky_xmpp_node_get_child (content, "description");
      if (description == NULL)
        {
          DEBUG ("Content %s is missing a description", name);
          continue;
        }

      mattr = wocky_xmpp_node_get_attribute (description, "media");
      if (mattr == NULL)
        {
          DEBUG ("Content %s is missing a media type", name);
          continue;
        }

      if (!tp_strdiff (mattr, "video"))
        {
          mtype = JINGLE_MEDIA_TYPE_VIDEO;
        }
      else if (!tp_strdiff (mattr, "audio"))
        {
          mtype = JINGLE_MEDIA_TYPE_AUDIO;
        }
      else
        {
          DEBUG ("Content %s has an unknown media type: %s", name, mattr);
          continue;
        }

      member_content = gabble_call_member_ensure_content (call_member,
        name, mtype);

      if (gabble_call_member_content_has_jingle_content (member_content))
        continue;

      codecs = call_muc_channel_parse_codecs (self, description);
      gabble_call_member_content_set_remote_codecs (member_content, codecs);

      if (!priv->initialized)
        {
          if (mtype == JINGLE_MEDIA_TYPE_AUDIO)
            g_object_set (self, "initial-audio", TRUE, NULL);
          else
            g_object_set (self, "initial-video", TRUE, NULL);
        }
    }
}

static void
call_muc_channel_presence_cb (WockyMuc *wmuc,
    WockyXmppStanza *stanza,
    GHashTable *code,
    WockyMucMember *who,
    gpointer user_data)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (user_data);

  call_muc_channel_got_participant_presence (self, who, stanza);
}

static void
call_muc_channel_update_all_members (GabbleCallMucChannel *self)
{
  GabbleCallMucChannelPrivate *priv = self->priv;
  GHashTable *members;
  GHashTableIter iter;
  gpointer value;

  members = wocky_muc_members (priv->wmuc);

  g_hash_table_iter_init (&iter, members);

  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      WockyMucMember *member = (WockyMucMember *) value;

      call_muc_channel_got_participant_presence (self,
        member, member->presence_stanza);
    }
}

static void
call_muc_channel_joined_cb (WockyMuc *muc,
  WockyXmppStanza *stanza,
  GHashTable *code,
  gpointer user_data)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (user_data);

  call_muc_channel_update_all_members (self);
}

static void
call_muc_channel_pre_presence_cb (WockyMuc *wmuc,
    WockyXmppStanza *stanza,
    gpointer user_data)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (user_data);
  WockyXmppNode *muji;
  GList *l;

  for (l = gabble_base_call_channel_get_contents (
      GABBLE_BASE_CALL_CHANNEL (self)); l != NULL; l = g_list_next (l))
    {
      GabbleCallContent *content = GABBLE_CALL_CONTENT (l->data);

      if (gabble_call_content_get_local_codecs (content) == NULL)
        return;
    }

  DEBUG ("%p got our a prepresence signal, putting in codecs", self);

  muji = wocky_xmpp_node_add_child_ns (stanza->node, "muji", NS_MUJI);
  for (l = gabble_base_call_channel_get_contents (
      GABBLE_BASE_CALL_CHANNEL (self)); l != NULL; l = g_list_next (l))
    {
      GabbleCallContent *content = GABBLE_CALL_CONTENT (l->data);
      const gchar *name = gabble_call_content_get_name (content);
      WockyXmppNode *mcontent;
      WockyXmppNode *description;
      GList *codecs;
      JingleMediaType mtype = gabble_call_content_get_media_type (content);

      mcontent = wocky_xmpp_node_add_child (muji, "content");
      wocky_xmpp_node_set_attribute (mcontent, "name", name);

      description = wocky_xmpp_node_add_child_ns (mcontent,
        "description", NS_JINGLE_RTP);
      wocky_xmpp_node_set_attribute (description, "media",
        mtype == JINGLE_MEDIA_TYPE_AUDIO ? "audio" : "video");

      for (codecs = gabble_call_content_get_local_codecs (content) ;
          codecs != NULL ; codecs = g_list_next (codecs))
        {
          JingleCodec *codec = codecs->data;
          WockyXmppNode *pt;
          GHashTableIter iter;
          gpointer key, value;
          gchar *idstr;

          pt = wocky_xmpp_node_add_child (description, "payload-type");

          idstr = g_strdup_printf ("%d", codec->id);
          wocky_xmpp_node_set_attribute (pt, "id", idstr);
          g_free (idstr);

          wocky_xmpp_node_set_attribute (pt, "name", codec->name);

          if (codec->clockrate > 0)
            {
              gchar *rate = g_strdup_printf ("%d", codec->clockrate);
              wocky_xmpp_node_set_attribute (pt, "clockrate", rate);
              g_free (rate);
            }

          if (codec->channels > 0)
            {
              gchar *channels = g_strdup_printf ("%d", codec->channels);
              wocky_xmpp_node_set_attribute (pt, "channels", channels);
              g_free (channels);
            }

          g_hash_table_iter_init (&iter, codec->params);
          while (g_hash_table_iter_next (&iter, &key, &value))
            {
              WockyXmppNode *p = wocky_xmpp_node_add_child (pt, "parameter");
              wocky_xmpp_node_set_attribute (p, "name", (gchar *) key);
              wocky_xmpp_node_set_attribute (p, "value", (gchar *) value);
            }
        }
    }
}

static void
call_muc_channel_own_presence_cb (WockyMuc *wmuc,
    WockyXmppStanza *stanza,
    GHashTable *code,
    gpointer user_data)
{
  DEBUG ("Got our own presence");
}

static void
call_muc_channel_ready (GabbleCallMucChannel *self)
{
  GabbleCallMucChannelPrivate *priv = self->priv;

  g_object_get (priv->muc, "wocky-muc", &(priv->wmuc), NULL);
  g_assert (priv->wmuc != NULL);

  if (wocky_muc_get_state (priv->wmuc) == WOCKY_MUC_JOINED)
    call_muc_channel_update_all_members (self);

  /* we care about presences */
  gabble_signal_connect_weak (priv->wmuc,
      "joined",
      G_CALLBACK (call_muc_channel_joined_cb),
      G_OBJECT (self));
  gabble_signal_connect_weak (priv->wmuc,
      "presence",
      G_CALLBACK (call_muc_channel_presence_cb),
      G_OBJECT (self));
  gabble_signal_connect_weak (priv->wmuc,
      "own-presence",
      G_CALLBACK (call_muc_channel_own_presence_cb),
      G_OBJECT (self));

  gabble_signal_connect_weak (priv->muc,
      "pre-presence",
      G_CALLBACK (call_muc_channel_pre_presence_cb),
      G_OBJECT (self));

  priv->initialized = TRUE;
  gabble_base_call_channel_register (GABBLE_BASE_CALL_CHANNEL (self));
}

static void
call_muc_channel_ready_cb (GabbleMucChannel *muc,
  gpointer user_data)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
  GabbleCallMucChannel *self =
    GABBLE_CALL_MUC_CHANNEL (g_async_result_get_source_object (
      G_ASYNC_RESULT (result)));

  DEBUG ("Happy muc");

  call_muc_channel_ready (self);
  g_simple_async_result_complete (result);
  g_object_unref (result);
  g_object_unref (self);
}

static void
call_muc_channel_init_async (GAsyncInitable *initable,
  int priority,
  GCancellable *cancellable,
  GAsyncReadyCallback callback,
  gpointer user_data)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (initable);
  GabbleCallMucChannelPrivate *priv =
    GABBLE_CALL_MUC_CHANNEL_GET_PRIVATE (self);
  GabbleBaseCallChannel *base = GABBLE_BASE_CALL_CHANNEL (self);
  GabbleCallContent *content;
  GSimpleAsyncResult *result;

  result = g_simple_async_result_new (G_OBJECT (initable),
      callback, user_data, NULL);

  if (base->initial_audio)
    {
      content = gabble_base_call_channel_add_content (base,
        "Audio", JINGLE_MEDIA_TYPE_AUDIO,
        GABBLE_CALL_CONTENT_DISPOSITION_INITIAL);
      call_muc_channel_setup_content (self, content);
    }

  if (base->initial_video)
    {
      content = gabble_base_call_channel_add_content (base,
        "Video", JINGLE_MEDIA_TYPE_VIDEO,
        GABBLE_CALL_CONTENT_DISPOSITION_INITIAL);
      call_muc_channel_setup_content (self, content);
    }

  if (_gabble_muc_channel_is_ready (priv->muc))
    {
      DEBUG ("Muc channel is ready to fly");
      call_muc_channel_ready (self);
      g_simple_async_result_complete_in_idle (result);
      g_object_unref (result);
    }
  else
    {
      DEBUG ("Muc channel isn't ready yet");
      gabble_signal_connect_weak (priv->muc,
        "ready", G_CALLBACK (call_muc_channel_ready_cb), G_OBJECT (result));
    }
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
  iface->init_async = call_muc_channel_init_async;
}

void
gabble_call_muc_channel_new_async (GabbleConnection *connection,
    const gchar *object_path,
    GabbleMucChannel *muc,
    TpHandle target,
    GHashTable *request,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  gboolean initial_audio = FALSE;
  gboolean initial_video = FALSE;

  DEBUG ("Starting initialisation of a Muji call channel");

  if (request != NULL)
    {
      initial_audio = tp_asv_get_boolean (request,
        GABBLE_IFACE_CHANNEL_TYPE_CALL ".InitialAudio", NULL);
      initial_video = tp_asv_get_boolean (request,
        GABBLE_IFACE_CHANNEL_TYPE_CALL ".InitialVideo", NULL);
    }

  g_async_initable_new_async (GABBLE_TYPE_CALL_MUC_CHANNEL,
    G_PRIORITY_DEFAULT,
    NULL,
    callback,
    user_data,
    "muc", muc,
    "object-path", object_path,
    "connection", connection,
    "handle", target,
    "requested", request != NULL,
    "initial-audio", initial_audio,
    "initial-video", initial_video,
    NULL);
}

GabbleCallMucChannel *
gabble_call_muc_channel_new_finish (GObject *source,
    GAsyncResult *result,
    GError **error)
{
  GObject *o;

  o = g_async_initable_new_finish (G_ASYNC_INITABLE (source), result, error);

  return o != NULL ? GABBLE_CALL_MUC_CHANNEL (o) : NULL;
}

void
gabble_call_muc_channel_incoming_session (GabbleCallMucChannel *self,
    GabbleJingleSession *session)
{
  GabbleCallMember *member;
  DEBUG ("New incoming session from %s",
    gabble_jingle_session_get_peer_jid (session));

  member = gabble_base_call_channel_get_member_from_handle
    (GABBLE_BASE_CALL_CHANNEL (self), session->peer);

  if (member == NULL || gabble_call_member_get_session (member) != NULL)
    {
      gabble_jingle_session_terminate (session,
        TP_CHANNEL_GROUP_CHANGE_REASON_NONE,
        "Muji jingle session initiated while there already was one",
        NULL);
    }
  else
    {
      gabble_call_member_set_session (member, session);
    }
}

static void
call_muc_channel_accept (GabbleBaseCallChannel *channel)
{
  GHashTable *members;
  GHashTableIter iter;
  gpointer value;

  DEBUG ("Accepted muji channel, starting sessions");

  members = gabble_base_call_channel_get_members (channel);

  g_hash_table_iter_init (&iter, members);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      GabbleCallMember *member = GABBLE_CALL_MEMBER (value);

      gabble_call_member_open_session (member, NULL);
      gabble_call_member_accept (member);
    }
}
