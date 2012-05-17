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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/gtypes.h>

#include <wocky/wocky.h>
#include "call-content.h"

#include "muc-channel.h"
#include "call-muc-channel.h"
#include "util.h"
#include "namespaces.h"
#include "jingle-tp-util.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"

static void async_initable_iface_init (GAsyncInitableIface *iface);

static void call_muc_channel_accept (TpBaseMediaCallChannel *channel);
static TpBaseCallContent * call_muc_channel_add_content (
    TpBaseCallChannel *base,
    const gchar *name,
    TpMediaStreamType type,
    TpMediaStreamDirection initial_direction,
    GError **error);
static void call_muc_channel_hangup (
    TpBaseCallChannel *base,
    guint reason,
    const gchar *detailed_reason,
    const gchar *message);

static void call_muc_channel_close (TpBaseChannel *base);

G_DEFINE_TYPE_WITH_CODE (GabbleCallMucChannel,
  gabble_call_muc_channel, GABBLE_TYPE_BASE_CALL_CHANNEL,
  G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init);
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
    tp_external_group_mixin_iface_init));

typedef enum
{
  STATE_NOT_JOINED = 0,
  /* Internally preparing before we can send muji information to the muc, only
   * happens on the initial join */
  STATE_PREPARING,
  /* Sent the stanza with the preparing node */
  STATE_PREPARING_SENT,
  /* We know when our turn is, now waiting for it */
  STATE_WAIT_FOR_TURN,
  /* Our state matches the state we published */
  STATE_STABLE,
  /* we left this muc */
  STATE_LEFT,
} MucCallState;

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
  MucCallState state;

  /* The list of members who should sent an update before us */
  GQueue *before;
  GQueue *after;

  /* List of members we should initial a session to after joining */
  GQueue *sessions_to_open;
  gboolean sessions_opened;

  GQueue *new_contents;

  /* Our current muji information */
  WockyNodeTree *muji;
};

typedef struct {
    GabbleCallMucChannel *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    gulong cancel_id;
    gulong ready_id;
} ChannelInitialisation;

static void
channel_init_free (ChannelInitialisation *ci)
{
  g_cancellable_disconnect (ci->cancellable, ci->cancel_id);

  tp_clear_object (&ci->cancellable);

  g_signal_handler_disconnect (ci->self->priv->muc, ci->ready_id);
  g_object_unref (ci->result);

  g_slice_free (ChannelInitialisation, ci);
}

static void
gabble_call_muc_channel_init (GabbleCallMucChannel *self)
{
  GabbleCallMucChannelPrivate *priv =
    G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CALL_MUC_CHANNEL, GabbleCallMucChannelPrivate);

  self->priv = priv;
  priv->before = g_queue_new ();
  priv->after = g_queue_new ();
  priv->sessions_to_open = g_queue_new ();
  priv->new_contents = g_queue_new ();
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
  TpBaseMediaCallChannelClass *base_media_call_class =
    TP_BASE_MEDIA_CALL_CHANNEL_CLASS (gabble_call_muc_channel_class);
  TpBaseCallChannelClass *base_call_class =
    TP_BASE_CALL_CHANNEL_CLASS (gabble_call_muc_channel_class);
  TpBaseChannelClass *base_channel_class =
      TP_BASE_CHANNEL_CLASS (gabble_call_muc_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_call_muc_channel_class,
    sizeof (GabbleCallMucChannelPrivate));

  object_class->set_property = gabble_call_muc_channel_set_property;
  object_class->get_property = gabble_call_muc_channel_get_property;

  object_class->constructed = gabble_call_muc_channel_constructed;
  object_class->dispose = gabble_call_muc_channel_dispose;
  object_class->finalize = gabble_call_muc_channel_finalize;

  base_channel_class->target_handle_type = TP_HANDLE_TYPE_ROOM;
  base_channel_class->close = call_muc_channel_close;

  base_call_class->add_content = call_muc_channel_add_content;
  base_call_class->hangup = call_muc_channel_hangup;

  base_media_call_class->accept = call_muc_channel_accept;

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
  GabbleCallMucChannelPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_clear_object (&priv->wmuc);
  tp_clear_object (&priv->muji);

  tp_external_group_mixin_finalize (object);

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_call_muc_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_muc_channel_parent_class)->dispose (object);
}

void
gabble_call_muc_channel_finalize (GObject *object)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (object);
  GabbleCallMucChannelPrivate *priv = self->priv;

  /* free any data held directly by the object here */
  g_queue_free (priv->before);
  g_queue_free (priv->after);
  g_queue_free (priv->sessions_to_open);
  g_queue_free (priv->new_contents);

  G_OBJECT_CLASS (gabble_call_muc_channel_parent_class)->finalize (object);
}

static gboolean
call_muc_channel_got_codecs (GabbleCallMucChannel *self)
{
  GList *l;

  for (l = tp_base_call_channel_get_contents (
      TP_BASE_CALL_CHANNEL (self)); l != NULL; l = g_list_next (l))
    {
      TpBaseMediaCallContent *content = TP_BASE_MEDIA_CALL_CONTENT (l->data);
      GHashTable *tp_md;
      GPtrArray *codecs;

      /* FIXME: remote_contact==0 ??? */
      tp_md = tp_base_media_call_content_get_local_media_description (content,
          0);
      codecs = tp_asv_get_boxed (tp_md,
          TP_PROP_CALL_CONTENT_MEDIA_DESCRIPTION_CODECS,
          TP_ARRAY_TYPE_CODEC_LIST);

      if (codecs == NULL)
        return FALSE;
    }

  return TRUE;
}

/* Decide on what to do next for an update */
static void
call_muc_do_update (GabbleCallMucChannel *self)
{
  GabbleCallMucChannelPrivate *priv = self->priv;
  MucCallState old = priv->state;

  switch (priv->state)
    {
      case STATE_NOT_JOINED:
      case STATE_PREPARING_SENT:
      case STATE_WAIT_FOR_TURN:
        /* we either didn't want to join yet or are already in the progress of
         * doing one, no need to take action */
        break;
      case STATE_PREPARING:
        g_assert (priv->muji == NULL);

        if (!call_muc_channel_got_codecs (self))
          {
            DEBUG ("Postponing sending prepare, waiting for codecs");
            break;
          }

        priv->muji = wocky_node_tree_new ("muji", NS_MUJI, NULL);
        /* fall through */
      case STATE_STABLE:
        /* Start preparation of the next round */
        g_assert (priv->muji != NULL);
        wocky_node_add_child (wocky_node_tree_get_top_node (priv->muji),
          "preparing");
        priv->state = STATE_PREPARING_SENT;
        gabble_muc_channel_send_presence (priv->muc);
        break;
      case STATE_LEFT:
        /* we left not doing anything */
        break;
    }

  DEBUG ("Updated muji state %d -> %d", old, priv->state);
}

static void
call_muc_channel_content_local_media_description_updated (
    GabbleCallContent *content,
    TpHandle contact,
    GHashTable *properties,
    gpointer user_data)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (user_data);

  DEBUG ("Local codecs of a content updated");
  call_muc_do_update (self);
}

static void
call_muc_channel_open_new_streams (GabbleCallMucChannel *self)
{
  GabbleCallMucChannelPrivate *priv = self->priv;
  GabbleCallMember *m;
  GabbleCallContent *c;

  if (!priv->sessions_opened)
    {
      /* At the point where we opened the sessions we're accepted
         in the call */
      tp_base_call_channel_set_state (TP_BASE_CALL_CHANNEL (self),
          TP_CALL_STATE_ACCEPTED,
          0, TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE, "", "");
    }

  priv->sessions_opened = TRUE;

  while ((m = g_queue_pop_head (priv->sessions_to_open)) != NULL)
    gabble_call_member_open_session (m, NULL);

  while ((c = g_queue_pop_head (priv->new_contents)) != NULL)
    {
      GList *l;

      l = gabble_call_content_get_member_contents (c);
      for (; l != NULL; l = g_list_next (l))
        {
          gabble_call_member_content_add_to_session (
              GABBLE_CALL_MEMBER_CONTENT (l->data));
        }
    }
}

static void
call_muc_channel_setup_content (GabbleCallMucChannel *self,
    GabbleCallContent *content)
{
  GabbleCallMucChannelPrivate *priv = self->priv;

  DEBUG ("Setting up content");

  gabble_signal_connect_weak (content, "local-media-description-updated",
    G_CALLBACK (call_muc_channel_content_local_media_description_updated),
    G_OBJECT (self));

  if (priv->sessions_opened)
    g_queue_push_tail (priv->new_contents, content);

  gabble_call_content_new_offer (content, NULL);
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

  for (l = tp_base_call_channel_get_contents (
      TP_BASE_CALL_CHANNEL (self)); l != NULL; l = g_list_next (l))
    {
      const char *cname;
      JingleMediaType cmtype;

      ccontent = GABBLE_CALL_CONTENT (l->data);
      cname = tp_base_call_content_get_name (
          TP_BASE_CALL_CONTENT (ccontent));
      cmtype = gabble_call_content_get_media_type (ccontent);

      if (!tp_strdiff (cname, name) && mtype == cmtype)
        goto have_content;
    }

  ccontent = gabble_base_call_channel_add_content (
      GABBLE_BASE_CALL_CHANNEL (self), name, mtype,
      self->priv->initialized ? TP_CALL_CONTENT_DISPOSITION_INITIAL : 0);
  call_muc_channel_setup_content (self, ccontent);

have_content:
  gabble_call_content_add_member_content (ccontent, content);
}

static GList *
call_muc_channel_parse_codecs (GabbleCallMucChannel *self,
    WockyNode *description)
{
  GList *codecs = NULL;
  WockyNodeIter iter;
  WockyNode *payload;

  wocky_node_iter_init (&iter, description,
      "payload-type", NS_JINGLE_RTP);
  while (wocky_node_iter_next (&iter, &payload))
    {
      const gchar *name;
      const gchar *value;
      guint id;
      guint clockrate = 0;
      guint channels = 0;
      JingleCodec *codec;
      WockyNodeIter param_iter;
      WockyNode *parameter;

      value = wocky_node_get_attribute (payload, "id");
      if (value == NULL)
        continue;
      id = atoi (value);

      name = wocky_node_get_attribute (payload, "name");
      if (name == NULL)
        continue;

      value = wocky_node_get_attribute (payload, "clockrate");
      if (value != NULL)
        clockrate = atoi (value);

      value = wocky_node_get_attribute (payload, "channels");
      if (value != NULL)
        channels = atoi (value);

      codec = jingle_media_rtp_codec_new (id, name, clockrate, channels, NULL);

      codecs = g_list_append (codecs, codec);

      wocky_node_iter_init (&param_iter, payload,
        "parameter", NS_JINGLE_RTP);
      while (wocky_node_iter_next (&param_iter, &parameter))
        {
          const gchar *key;

          key = wocky_node_get_attribute (parameter, "name");
          value = wocky_node_get_attribute (parameter, "value");

          if (key == NULL || value == NULL)
            continue;

          g_hash_table_insert (codec->params,
              g_strdup (key), g_strdup (value));
        }
    }

  return codecs;
}

static void
call_muc_channel_send_new_state (GabbleCallMucChannel *self)
{
  GabbleCallMucChannelPrivate *priv = self->priv;
  /* Our turn! */
  GQueue *t;
  WockyNode *m;
  GList *l;

  /* switch the before and after queues */
  t = priv->before;
  priv->before = priv->after;
  priv->after = t;

  g_object_unref (priv->muji);
  priv->muji = wocky_node_tree_new ("muji", NS_MUJI, '*', &m, NULL);

  for (l = tp_base_call_channel_get_contents (
      TP_BASE_CALL_CHANNEL (self)); l != NULL; l = g_list_next (l))
    {
      GabbleCallContent *content = GABBLE_CALL_CONTENT (l->data);
      const gchar *name = tp_base_call_content_get_name (
          TP_BASE_CALL_CONTENT (content));
      WockyNode *description;
      GHashTable *tp_md;
      GPtrArray *codecs;
      guint i;
      JingleMediaType mtype = gabble_call_content_get_media_type (content);


      wocky_node_add_build (m,
        '(', "content", '@', "name", name,
          '(', "description", ':', NS_JINGLE_RTP, '*', &description,
            '@', "media", mtype == JINGLE_MEDIA_TYPE_AUDIO ? "audio" : "video",
          ')',
        ')',
        NULL);

      /* FIXME: remote_contact==0 ??? */
      tp_md = tp_base_media_call_content_get_local_media_description (
          TP_BASE_MEDIA_CALL_CONTENT (content), 0);
      codecs = tp_asv_get_boxed (tp_md,
          TP_PROP_CALL_CONTENT_MEDIA_DESCRIPTION_CODECS,
          TP_ARRAY_TYPE_CODEC_LIST);
      for (i = 0; i < codecs->len; i++)
        {
          GValueArray *codec = g_ptr_array_index (codecs, i);
          WockyNode *pt;
          GHashTableIter iter;
          gpointer key, value;
          gchar *idstr;
          guint v;

          idstr = g_strdup_printf ("%d",
            g_value_get_uint (codec->values));
          wocky_node_add_build (description,
            '(', "payload-type", '*', &pt,
                '@', "id", idstr,
                '@', "name", g_value_get_string (codec->values + 1),
             ')',
            NULL);
          g_free (idstr);

          /* clock-rate */
          v = g_value_get_uint (codec->values + 2);
          if (v > 0)
            {
              gchar *rate = g_strdup_printf ("%d", v);
              wocky_node_set_attribute (pt, "clockrate", rate);
              g_free (rate);
            }

          /* channels */
          v = g_value_get_uint (codec->values + 3);
          if (v > 0)
            {
              gchar *channels = g_strdup_printf ("%d", v);
              wocky_node_set_attribute (pt, "channels", channels);
              g_free (channels);
            }

          g_hash_table_iter_init (&iter,
            g_value_get_boxed (codec->values + 5));
          while (g_hash_table_iter_next (&iter, &key, &value))
              wocky_node_add_build (pt,
                '(', "parameter",
                  '@', "name", (gchar *) key,
                  '@', "value", (gchar *) value,
                ')',
                NULL);
        }
    }

  priv->state = STATE_STABLE;
  gabble_muc_channel_send_presence (priv->muc);
}

static void
call_muc_channel_parse_participant (GabbleCallMucChannel *self,
  GabbleCallMember *member,
  WockyNode *muji)
{
  GabbleCallMucChannelPrivate *priv = self->priv;
  WockyNodeIter iter;
  WockyNode *content;

  wocky_node_iter_init (&iter, muji, "content", NS_MUJI);
  while (wocky_node_iter_next (&iter, &content))
    {
      GabbleCallMemberContent *member_content;
      WockyNode *description;
      JingleMediaType mtype;
      const gchar *name;
      const gchar *mattr;
      GList *codecs;

      name = wocky_node_get_attribute (content, "name");
      if (name == NULL)
        {
          DEBUG ("Content is missing the name attribute");
          continue;
        }

      DEBUG ("Parsing content: %s", name);

      description = wocky_node_get_child (content, "description");
      if (description == NULL)
        {
          DEBUG ("Content %s is missing a description", name);
          continue;
        }

      mattr = wocky_node_get_attribute (description, "media");
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

      member_content = gabble_call_member_ensure_content (member,
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
call_muc_channel_remove_member (GabbleCallMucChannel *self,
  GabbleCallMember *call_member)
{
  GabbleCallMucChannelPrivate *priv = self->priv;

   g_queue_remove (priv->before, call_member);
   g_queue_remove (priv->after, call_member);
   g_queue_remove (priv->sessions_to_open, call_member);

   gabble_base_call_channel_remove_member (
      GABBLE_BASE_CALL_CHANNEL (self), call_member);
}

static void
call_muc_channel_got_participant_presence (GabbleCallMucChannel *self,
  WockyMucMember *member,
  WockyStanza *stanza)
{
  GabbleCallMucChannelPrivate *priv = self->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      tp_base_channel_get_connection (TP_BASE_CHANNEL (self)),
      TP_HANDLE_TYPE_CONTACT);
  GabbleCallMember *call_member;
  TpHandle handle;
  WockyNode *muji;

  muji = wocky_node_get_child_ns (
      wocky_stanza_get_top_node (stanza), "muji", NS_MUJI);

  DEBUG ("Muji participant: %s", member->from);

  handle = tp_handle_ensure (contact_repo, member->from, NULL, NULL);

  call_member = gabble_base_call_channel_get_member_from_handle (
    GABBLE_BASE_CALL_CHANNEL (self), handle);

  if (muji == NULL)
    {
      /* Member without muji information remove it if needed otherwise
         ignore */
      if (call_member != NULL)
        call_muc_channel_remove_member (self, call_member);
      return;
    }

  if (call_member == NULL)
    {
      call_member = gabble_base_call_channel_ensure_member_from_handle (
        GABBLE_BASE_CALL_CHANNEL (self), handle);
      gabble_signal_connect_weak (call_member, "content-added",
        G_CALLBACK (call_muc_channel_member_content_added_cb),
        G_OBJECT (self));
      gabble_call_member_accept (call_member);
    }

  if (!priv->sessions_opened && priv->state < STATE_WAIT_FOR_TURN)
    g_queue_push_tail (priv->sessions_to_open, call_member);

  call_muc_channel_parse_participant (self, call_member, muji);

  if (wocky_node_get_child (muji, "preparing"))
    {
      /* remote member is preparing something, add to the right queue */
      if (!g_queue_find (priv->before, call_member)
          && !g_queue_find (priv->after, call_member))
        {
          g_queue_push_tail (
              priv->state != STATE_WAIT_FOR_TURN ? priv->before : priv->after,
              call_member);
        }
    }
  else
    {
      /* remote member isn't preparing or at least not anymore */
      g_queue_remove (priv->before, call_member);
      g_queue_remove (priv->after, call_member);
      if (priv->state == STATE_WAIT_FOR_TURN &&
          g_queue_is_empty (priv->before))
        {
          call_muc_channel_send_new_state (self);
        }
    }

}

static void
call_muc_channel_presence_cb (WockyMuc *wmuc,
    WockyStanza *stanza,
    guint codes,
    WockyMucMember *who,
    gpointer user_data)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (user_data);

  call_muc_channel_got_participant_presence (self, who, stanza);
}

static void
call_muc_channel_left_cb (GObject *source,
  WockyStanza *stanza,
  guint codes,
  WockyMucMember *member,
  const gchar *actor_jid,
  const gchar *why,
  const gchar *msg,
  gpointer user_data)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (user_data);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      tp_base_channel_get_connection (TP_BASE_CHANNEL (self)),
      TP_HANDLE_TYPE_CONTACT);
  TpHandle handle;
  GabbleCallMember *call_member;

  handle = tp_handle_ensure (contact_repo, member->from, NULL, NULL);
  call_member = gabble_base_call_channel_get_member_from_handle (
    GABBLE_BASE_CALL_CHANNEL (self), handle);

  DEBUG ("%s left the room, %p", member->from, call_member);

  if (call_member != NULL)
    call_muc_channel_remove_member (self, call_member);
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

  g_hash_table_unref (members);
}

static void
call_muc_channel_joined_cb (WockyMuc *muc,
  WockyStanza *stanza,
  guint codes,
  gpointer user_data)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (user_data);

  call_muc_channel_update_all_members (self);
}

static void
call_muc_channel_pre_presence_cb (WockyMuc *wmuc,
    WockyStanza *stanza,
    gpointer user_data)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (user_data);

  if (self->priv->muji == NULL)
    return;

  wocky_node_add_node_tree (wocky_stanza_get_top_node (stanza),
    self->priv->muji);
}

static void
call_muc_channel_own_presence_cb (WockyMuc *wmuc,
    WockyStanza *stanza,
    guint codes,
    gpointer user_data)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (user_data);
  GabbleCallMucChannelPrivate *priv = self->priv;
  WockyNode *muji;

  DEBUG ("Got our own presence");

  muji = wocky_node_get_child_ns (
    wocky_stanza_get_top_node (stanza), "muji", NS_MUJI);

  /* If our presence didn't have a muji stanza or had an older version we don't
   * care about it */
  if (muji == NULL || priv->muji == NULL ||
      !wocky_node_equal (muji, wocky_node_tree_get_top_node (priv->muji)))
    return;

  switch (priv->state)
    {
      case STATE_PREPARING_SENT:
        DEBUG ("Got our preperation message, now waiting for our turn");
        priv->state = STATE_WAIT_FOR_TURN;

        if (g_queue_is_empty (priv->before))
          call_muc_channel_send_new_state (self);
        break;
      case STATE_WAIT_FOR_TURN:
        break;
      case STATE_STABLE:
        call_muc_channel_open_new_streams (self);
        break;
      default:
        DEBUG ("Got a muji presence from ourselves before we sent one ?!");
    }
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
  gabble_signal_connect_weak (priv->wmuc,
      "left",
      G_CALLBACK (call_muc_channel_left_cb),
      G_OBJECT (self));

  gabble_signal_connect_weak (priv->muc,
      "pre-presence",
      G_CALLBACK (call_muc_channel_pre_presence_cb),
      G_OBJECT (self));

  priv->initialized = TRUE;
  tp_base_channel_register (TP_BASE_CHANNEL (self));
}

static void
call_muc_channel_cancelled_cb (GCancellable *cancellable,
    gpointer user_data)
{
  ChannelInitialisation *ci = user_data;

  DEBUG ("Cancelled");

  g_simple_async_result_set_error (ci->result,
    G_IO_ERROR, G_IO_ERROR_CANCELLED, "Channel request was cancelled");
  g_simple_async_result_complete (ci->result);

  /* called, don't disconnect */
  ci->cancel_id = 0;

  channel_init_free (ci);
}

static void
call_muc_channel_ready_cb (GabbleMucChannel *muc,
  gpointer user_data)
{
  ChannelInitialisation *ci = user_data;

  DEBUG ("Happy muc");

  call_muc_channel_ready (ci->self);

  g_simple_async_result_complete (ci->result);
  channel_init_free (ci);
}

static void
call_muc_channel_init_async (GAsyncInitable *initable,
  int priority,
  GCancellable *cancellable,
  GAsyncReadyCallback callback,
  gpointer user_data)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (initable);
  GabbleCallMucChannelPrivate *priv = self->priv;
  TpBaseCallChannel *base = TP_BASE_CALL_CHANNEL (self);
  GabbleCallContent *content;
  GSimpleAsyncResult *result;
  gboolean initial_audio, initial_video;
  const gchar *initial_audio_name, *initial_video_name;

  initial_audio = tp_base_call_channel_has_initial_audio (base,
      &initial_audio_name);
  initial_video = tp_base_call_channel_has_initial_video (base,
      &initial_video_name);

  result = g_simple_async_result_new (G_OBJECT (initable),
      callback, user_data, NULL);

  if (initial_audio)
    {
      content = gabble_base_call_channel_add_content (
        GABBLE_BASE_CALL_CHANNEL (base),
        initial_audio_name, JINGLE_MEDIA_TYPE_AUDIO,
        TP_CALL_CONTENT_DISPOSITION_INITIAL);
      call_muc_channel_setup_content (self, content);
    }

  if (initial_video)
    {
      content = gabble_base_call_channel_add_content (
        GABBLE_BASE_CALL_CHANNEL (base),
        initial_video_name, JINGLE_MEDIA_TYPE_VIDEO,
        TP_CALL_CONTENT_DISPOSITION_INITIAL);
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
      ChannelInitialisation *ci = g_slice_new0 (ChannelInitialisation);

      DEBUG ("Muc channel isn't ready yet");

      ci->self = self;
      ci->result = result;

      ci->ready_id = g_signal_connect (priv->muc,
        "ready", G_CALLBACK (call_muc_channel_ready_cb), ci);

      if (cancellable != NULL)
        {
          ci->cancellable = g_object_ref (cancellable);
          ci->cancel_id = g_cancellable_connect (cancellable,
            G_CALLBACK (call_muc_channel_cancelled_cb), ci, NULL);
        }
    }
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
  iface->init_async = call_muc_channel_init_async;
}

void
gabble_call_muc_channel_new_async (GabbleConnection *connection,
    GCancellable *cancellable,
    const gchar *path_prefix,
    GabbleMucChannel *muc,
    TpHandle target,
    GHashTable *request,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  gboolean initial_audio = FALSE;
  gboolean initial_video = FALSE;
  const gchar *initial_audio_name = NULL;
  const gchar *initial_video_name = NULL;

  DEBUG ("Starting initialisation of a Muji call channel");

  if (request != NULL)
    {
      initial_audio = tp_asv_get_boolean (request,
          TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO, NULL);
      initial_video = tp_asv_get_boolean (request,
          TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO, NULL);

      initial_audio_name = tp_asv_get_string (request,
          TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO_NAME);
      initial_video_name = tp_asv_get_string (request,
          TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO_NAME);
    }

  g_async_initable_new_async (GABBLE_TYPE_CALL_MUC_CHANNEL,
    G_PRIORITY_DEFAULT,
    cancellable,
    callback,
    user_data,
    "muc", muc,
    "object-path-prefix", path_prefix,
    "connection", connection,
    "handle", target,
    "requested", request != NULL,
    "mutable-contents", TRUE,
    "initial-audio", initial_audio,
    "initial-audio-name",
       initial_audio_name != NULL ? initial_audio_name : "audio",
    "initial-video", initial_video,
    "initial-video-name",
      initial_video_name != NULL ? initial_video_name : "video",
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
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      tp_base_channel_get_connection (TP_BASE_CHANNEL (self)),
      TP_HANDLE_TYPE_CONTACT);
  const gchar *jid = gabble_jingle_session_get_peer_jid (session);
  TpHandle peer = tp_handle_ensure (contact_repo, jid, NULL, NULL);

  DEBUG ("New incoming session from %s", jid);
  member = gabble_base_call_channel_get_member_from_handle (
      GABBLE_BASE_CALL_CHANNEL (self), peer);

  if (member == NULL || gabble_call_member_get_session (member) != NULL)
    {
      gabble_jingle_session_terminate (session,
        JINGLE_REASON_UNKNOWN,
        "Muji jingle session initiated while there already was one",
        NULL);
    }
  else
    {
      gabble_call_member_set_session (member, session);
    }
}

static void
call_muc_channel_accept (TpBaseMediaCallChannel *channel)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (channel);

  if (self->priv->state != STATE_NOT_JOINED)
    return;

  DEBUG ("Accepted muji channel");

  /* Start preparing to join the conference */
  self->priv->state = STATE_PREPARING;
  call_muc_do_update (self);
}

static TpBaseCallContent *
call_muc_channel_add_content (TpBaseCallChannel *base,
    const gchar *name,
    TpMediaStreamType type,
    TpMediaStreamDirection initial_direction,
    GError **error)
{
  GabbleCallMucChannel *self = GABBLE_CALL_MUC_CHANNEL (base);
  GabbleCallContent *content;

  if (initial_direction == TP_MEDIA_STREAM_DIRECTION_NONE)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Jingle can not do contents with direction = NONE");
      return NULL;
    }

  if (initial_direction != TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
          "Adding un-directional contents is not supported"
          " in MUC channels");
      return NULL;
    }

  content = gabble_base_call_channel_add_content (
        GABBLE_BASE_CALL_CHANNEL (base),
        name, jingle_media_type_from_tp (type),
        TP_CALL_CONTENT_DISPOSITION_NONE);

  call_muc_channel_setup_content (self, content);

  return TP_BASE_CALL_CONTENT (content);
}

static void
call_muc_channel_leave (GabbleCallMucChannel *self)
{
  GabbleCallMucChannelPrivate *priv = self->priv;

  if (priv->state == STATE_LEFT)
    return;

  tp_clear_object (&priv->muji);

  priv->state = STATE_LEFT;
  gabble_muc_channel_send_presence (priv->muc);
}

static void
call_muc_channel_hangup (TpBaseCallChannel *base,
    guint reason,
    const gchar *detailed_reason,
    const gchar *message)
{
  TpBaseCallChannelClass *parent = TP_BASE_CALL_CHANNEL_CLASS (
    gabble_call_muc_channel_parent_class);
  call_muc_channel_leave (GABBLE_CALL_MUC_CHANNEL (base));

  if (parent->hangup != NULL)
    parent->hangup (base, reason, detailed_reason, message);
}

static void
call_muc_channel_close (TpBaseChannel *base)
{
  call_muc_channel_leave (GABBLE_CALL_MUC_CHANNEL (base));

  TP_BASE_CHANNEL_CLASS (gabble_call_muc_channel_parent_class)->close (base);
}
