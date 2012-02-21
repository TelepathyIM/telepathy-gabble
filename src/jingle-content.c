/*
 * gabble-jingle-content.c - Source for WockyJingleContent
 * Copyright (C) 2008 Collabora Ltd.
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
#include "jingle-content.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "connection.h"
#include "debug.h"
#include "jingle-factory.h"
#include "jingle-session.h"
#include "jingle-transport-iface.h"
#include "jingle-transport-google.h"
#include "jingle-media-rtp.h"
#include "namespaces.h"
#include "gabble-signals-marshal.h"

/* signal enum */
enum
{
  READY,
  NEW_CANDIDATES,
  REMOVED,
  NEW_SHARE_CHANNEL,
  COMPLETED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_SESSION = 1,
  PROP_CONTENT_NS,
  PROP_TRANSPORT_NS,
  PROP_NAME,
  PROP_SENDERS,
  PROP_STATE,
  PROP_DISPOSITION,
  PROP_LOCALLY_CREATED,
  LAST_PROPERTY
};

struct _WockyJingleContentPrivate
{
  gchar *name;
  gchar *creator;
  gboolean created_by_us;
  WockyJingleContentState state;
  WockyJingleContentSenders senders;

  gchar *content_ns;
  gchar *transport_ns;
  gchar *disposition;

  WockyJingleTransportIface *transport;

  /* Whether we've got the codecs (intersection) ready. */
  gboolean media_ready;

  /* Whether we have at least one local candidate. */
  gboolean have_local_candidates;

  guint gtalk4_event_id;
  guint last_share_channel_component_id;

  gboolean dispose_has_run;
};

#define DEFAULT_CONTENT_TIMEOUT 60000

/* lookup tables */

G_DEFINE_TYPE(WockyJingleContent, wocky_jingle_content, G_TYPE_OBJECT);

static void new_transport_candidates_cb (WockyJingleTransportIface *trans,
    GList *candidates, WockyJingleContent *content);
static void _maybe_ready (WockyJingleContent *self);
static void transport_created (WockyJingleContent *c);

static void
wocky_jingle_content_init (WockyJingleContent *obj)
{
  WockyJingleContentPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, WOCKY_TYPE_JINGLE_CONTENT,
         WockyJingleContentPrivate);
  obj->priv = priv;

  DEBUG ("%p", obj);

  priv->state = WOCKY_JINGLE_CONTENT_STATE_EMPTY;
  priv->created_by_us = TRUE;
  priv->media_ready = FALSE;
  priv->have_local_candidates = FALSE;
  priv->gtalk4_event_id = 0;
  priv->dispose_has_run = FALSE;

  obj->session = NULL;
}

static void
wocky_jingle_content_dispose (GObject *object)
{
  WockyJingleContent *content = WOCKY_JINGLE_CONTENT (object);
  WockyJingleContentPrivate *priv = content->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("%p", object);
  priv->dispose_has_run = TRUE;

  if (priv->gtalk4_event_id != 0)
    {
      g_source_remove (priv->gtalk4_event_id);
      priv->gtalk4_event_id = 0;
    }

  g_free (priv->name);
  priv->name = NULL;

  g_free (priv->creator);
  priv->creator = NULL;

  g_free (priv->content_ns);
  priv->content_ns = NULL;

  g_free (priv->transport_ns);
  priv->transport_ns = NULL;

  g_free (priv->disposition);
  priv->disposition = NULL;

  if (G_OBJECT_CLASS (wocky_jingle_content_parent_class)->dispose)
    G_OBJECT_CLASS (wocky_jingle_content_parent_class)->dispose (object);
}

static void
wocky_jingle_content_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  WockyJingleContent *self = WOCKY_JINGLE_CONTENT (object);
  WockyJingleContentPrivate *priv = self->priv;

  switch (property_id) {
    case PROP_SESSION:
      g_value_set_object (value, self->session);
      break;
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    case PROP_SENDERS:
      g_value_set_uint (value, priv->senders);
      break;
    case PROP_STATE:
      g_value_set_uint (value, priv->state);
      break;
    case PROP_CONTENT_NS:
      g_value_set_string (value, priv->content_ns);
      break;
    case PROP_TRANSPORT_NS:
      g_value_set_string (value, priv->transport_ns);
      break;
    case PROP_DISPOSITION:
      g_value_set_string (value, priv->disposition);
      break;
    case PROP_LOCALLY_CREATED:
      g_value_set_boolean (value, priv->created_by_us);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
wocky_jingle_content_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  WockyJingleContent *self = WOCKY_JINGLE_CONTENT (object);
  WockyJingleContentPrivate *priv = self->priv;

  switch (property_id) {
    case PROP_SESSION:
      self->session = g_value_get_object (value);
      break;
    case PROP_CONTENT_NS:
      g_free (priv->content_ns);
      priv->content_ns = g_value_dup_string (value);
      break;
    case PROP_TRANSPORT_NS:
      g_free (priv->transport_ns);
      priv->transport_ns = g_value_dup_string (value);

      /* We can't switch transports. */
      g_assert (priv->transport == NULL);

      if (priv->transport_ns != NULL)
        {
          GType transport_type = wocky_jingle_factory_lookup_transport (
              wocky_jingle_session_get_factory (self->session),
              priv->transport_ns);

          g_assert (transport_type != 0);

          priv->transport = wocky_jingle_transport_iface_new (transport_type,
              self, priv->transport_ns);

          g_signal_connect (priv->transport, "new-candidates",
              (GCallback) new_transport_candidates_cb, self);

          transport_created (self);
        }
      break;
    case PROP_NAME:
      /* can't rename */
      g_assert (priv->name == NULL);

      priv->name = g_value_dup_string (value);
      break;
    case PROP_SENDERS:
      priv->senders = g_value_get_uint (value);
      break;
    case PROP_STATE:
      priv->state = g_value_get_uint (value);
      break;
    case PROP_DISPOSITION:
      g_assert (priv->disposition == NULL);
      priv->disposition = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static WockyJingleContentSenders
get_default_senders_real (WockyJingleContent *c)
{
  return WOCKY_JINGLE_CONTENT_SENDERS_BOTH;
}


static void
wocky_jingle_content_class_init (WockyJingleContentClass *cls)
{
  GParamSpec *param_spec;
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  g_type_class_add_private (cls, sizeof (WockyJingleContentPrivate));

  object_class->get_property = wocky_jingle_content_get_property;
  object_class->set_property = wocky_jingle_content_set_property;
  object_class->dispose = wocky_jingle_content_dispose;

  cls->get_default_senders = get_default_senders_real;

  /* property definitions */
  param_spec = g_param_spec_object ("session", "WockyJingleSession object",
      "Jingle session object that owns this content.",
      WOCKY_TYPE_JINGLE_SESSION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SESSION, param_spec);

  param_spec = g_param_spec_string ("name", "Content name",
      "A unique content name in the session.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NAME, param_spec);

  param_spec = g_param_spec_string ("content-ns", "Content namespace",
      "Namespace identifying the content type.",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTENT_NS, param_spec);

  param_spec = g_param_spec_string ("transport-ns", "Transport namespace",
      "Namespace identifying the transport type.",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TRANSPORT_NS, param_spec);

  param_spec = g_param_spec_uint ("senders", "Stream senders",
      "Valid senders for the stream.",
      0, G_MAXUINT32, WOCKY_JINGLE_CONTENT_SENDERS_NONE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SENDERS, param_spec);

  param_spec = g_param_spec_uint ("state", "Content state",
      "The current state that the content is in.",
      0, G_MAXUINT32, WOCKY_JINGLE_CONTENT_STATE_EMPTY,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  param_spec = g_param_spec_string ("disposition", "Content disposition",
      "Distinguishes between 'session' and other contents.",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DISPOSITION, param_spec);

  param_spec = g_param_spec_boolean ("locally-created", "Locally created",
      "True if the content was created by the local client.",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LOCALLY_CREATED, param_spec);

  /* signal definitions */

  signals[READY] = g_signal_new ("ready",
    G_OBJECT_CLASS_TYPE (cls),
    G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__VOID,
    G_TYPE_NONE, 0);

  signals[NEW_CANDIDATES] = g_signal_new (
    "new-candidates",
    G_TYPE_FROM_CLASS (cls),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);

  signals[NEW_SHARE_CHANNEL] = g_signal_new (
    "new-share-channel",
    G_TYPE_FROM_CLASS (cls),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    gabble_marshal_VOID__STRING_UINT,
    G_TYPE_NONE,
    2,
    G_TYPE_STRING, G_TYPE_UINT);

  signals[COMPLETED] = g_signal_new (
    "completed",
    G_TYPE_FROM_CLASS (cls),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__VOID,
    G_TYPE_NONE,
    0);

  /* This signal serves as notification that the WockyJingleContent is now
   * meaningless; everything holding a reference should drop it after receiving
   * 'removed'.
   */
  signals[REMOVED] = g_signal_new ("removed",
    G_OBJECT_CLASS_TYPE (cls),
    G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
    0,
    NULL, NULL,
    g_cclosure_marshal_VOID__VOID,
    G_TYPE_NONE, 0);
}


static WockyJingleContentSenders
get_default_senders (WockyJingleContent *c)
{
  WockyJingleContentSenders (*virtual_method)(WockyJingleContent *) = \
      WOCKY_JINGLE_CONTENT_GET_CLASS (c)->get_default_senders;

  g_assert (virtual_method != NULL);
  return virtual_method (c);
}


static WockyJingleContentSenders
parse_senders (const gchar *txt)
{
  if (txt == NULL)
      return WOCKY_JINGLE_CONTENT_SENDERS_NONE;

  if (!wocky_strdiff (txt, "initiator"))
      return WOCKY_JINGLE_CONTENT_SENDERS_INITIATOR;
  else if (!wocky_strdiff (txt, "responder"))
      return WOCKY_JINGLE_CONTENT_SENDERS_RESPONDER;
  else if (!wocky_strdiff (txt, "both"))
      return WOCKY_JINGLE_CONTENT_SENDERS_BOTH;

  return WOCKY_JINGLE_CONTENT_SENDERS_NONE;
}

static const gchar *
produce_senders (WockyJingleContentSenders senders)
{
  switch (senders) {
    case WOCKY_JINGLE_CONTENT_SENDERS_INITIATOR:
      return "initiator";
    case WOCKY_JINGLE_CONTENT_SENDERS_RESPONDER:
      return "responder";
    case WOCKY_JINGLE_CONTENT_SENDERS_BOTH:
      return "both";
    default:
      DEBUG ("invalid content senders %u", senders);
      g_assert_not_reached ();
  }

  /* to make gcc not complain */
  return NULL;
}


#define SET_BAD_REQ(txt) \
    g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST, txt)

static void
new_transport_candidates_cb (WockyJingleTransportIface *trans,
    GList *candidates, WockyJingleContent *content)
{
  /* just pass the signal on */
  g_signal_emit (content, signals[NEW_CANDIDATES], 0, candidates);
}

static void
transport_created (WockyJingleContent *c)
{
  void (*virtual_method)(WockyJingleContent *, WockyJingleTransportIface *) = \
      WOCKY_JINGLE_CONTENT_GET_CLASS (c)->transport_created;

  if (virtual_method != NULL)
    virtual_method (c, c->priv->transport);
}


static void
parse_description (WockyJingleContent *c, WockyNode *desc_node,
    GError **error)
{
  void (*virtual_method)(WockyJingleContent *, WockyNode *,
      GError **) = WOCKY_JINGLE_CONTENT_GET_CLASS (c)->parse_description;

  g_assert (virtual_method != NULL);
  virtual_method (c, desc_node, error);
}

static gboolean
send_gtalk4_transport_accept (gpointer user_data)
{
  WockyJingleContent *c = WOCKY_JINGLE_CONTENT (user_data);
  WockyJingleContentPrivate *priv = c->priv;
  WockyNode *sess_node;
  WockyStanza *msg = wocky_jingle_session_new_message (c->session,
      WOCKY_JINGLE_ACTION_TRANSPORT_ACCEPT, &sess_node);

  DEBUG ("Sending Gtalk4 'transport-accept' message to peer");
  wocky_node_add_child_ns (sess_node, "transport", priv->transport_ns);

  wocky_jingle_session_send (c->session, msg);

  return FALSE;
}

void
wocky_jingle_content_parse_add (WockyJingleContent *c,
    WockyNode *content_node, gboolean google_mode, GError **error)
{
  WockyJingleContentPrivate *priv = c->priv;
  const gchar *name, *creator, *senders, *disposition;
  WockyNode *trans_node, *desc_node;
  GType transport_type = 0;
  WockyJingleTransportIface *trans = NULL;
  WockyJingleDialect dialect = wocky_jingle_session_get_dialect (c->session);

  priv->created_by_us = FALSE;

  desc_node = wocky_node_get_child (content_node, "description");
  trans_node = wocky_node_get_child (content_node, "transport");
  creator = wocky_node_get_attribute (content_node, "creator");
  name = wocky_node_get_attribute (content_node, "name");
  senders = wocky_node_get_attribute (content_node, "senders");

  g_assert (priv->transport_ns == NULL);

  if (google_mode)
    {
      if (creator == NULL)
          creator = "initiator";

      /* the google protocols don't give the contents names, so put in a dummy
       * value if none was set by the session*/
      if (priv->name == NULL)
        name = priv->name = g_strdup ("gtalk");
      else
        name = priv->name;

      if (trans_node == NULL)
        {
          /* gtalk lj0.3 assumes google-p2p transport */
          DEBUG ("detected GTalk3 dialect");

          dialect = WOCKY_JINGLE_DIALECT_GTALK3;
          g_object_set (c->session, "dialect", WOCKY_JINGLE_DIALECT_GTALK3, NULL);
          transport_type = wocky_jingle_factory_lookup_transport (
              wocky_jingle_session_get_factory (c->session),
              "");

          /* in practice we do support gtalk-p2p, so this can't happen */
          if (G_UNLIKELY (transport_type == 0))
            {
              SET_BAD_REQ ("gtalk-p2p transport unsupported");
              return;
            }

          priv->transport_ns = g_strdup ("");
        }
    }
  else
    {
      if (creator == NULL &&
          wocky_jingle_session_peer_has_cap (c->session,
              QUIRK_GOOGLE_WEBMAIL_CLIENT))
        {
          if (wocky_jingle_content_creator_is_initiator (c))
            creator = "initiator";
          else
            creator = "responder";

          DEBUG ("Working around GMail omitting creator=''; assuming '%s'",
              creator);
        }

      if ((trans_node == NULL) || (creator == NULL) || (name == NULL))
        {
          SET_BAD_REQ ("missing required content attributes or elements");
          return;
        }

      /* In proper protocols the name comes from the stanza */
      g_assert (priv->name == NULL);
      priv->name = g_strdup (name);
    }

  /* if we didn't set it to google-p2p implicitly already, detect it */
  if (transport_type == 0)
    {
      const gchar *ns = wocky_node_get_ns (trans_node);

      transport_type = wocky_jingle_factory_lookup_transport (
          wocky_jingle_session_get_factory (c->session), ns);

      if (transport_type == 0)
        {
          SET_BAD_REQ ("unsupported content transport");
          return;
        }

      priv->transport_ns = g_strdup (ns);
    }

  if (senders == NULL)
    priv->senders = get_default_senders (c);
  else
    priv->senders = parse_senders (senders);

  if (priv->senders == WOCKY_JINGLE_CONTENT_SENDERS_NONE)
    {
      SET_BAD_REQ ("invalid content senders");
      return;
    }

  parse_description (c, desc_node, error);
  if (*error != NULL)
      return;

  disposition = wocky_node_get_attribute (content_node, "disposition");
  if (disposition == NULL)
      disposition = "session";

  if (wocky_strdiff (disposition, priv->disposition))
    {
      g_free (priv->disposition);
      priv->disposition = g_strdup (disposition);
    }

  DEBUG ("content creating new transport type %s", g_type_name (transport_type));

  trans = wocky_jingle_transport_iface_new (transport_type,
      c, priv->transport_ns);

  g_signal_connect (trans, "new-candidates",
      (GCallback) new_transport_candidates_cb, c);

  /* Depending on transport, there may be initial candidates specified here */
  if (trans_node != NULL)
    {
      wocky_jingle_transport_iface_parse_candidates (trans, trans_node, error);
      if (*error)
        {
          g_object_unref (trans);
          return;
        }
    }

  g_assert (priv->transport == NULL);
  priv->transport = trans;
  transport_created (c);

  g_assert (priv->creator == NULL);
  priv->creator = g_strdup (creator);

  priv->state = WOCKY_JINGLE_CONTENT_STATE_NEW;

  /* GTalk4 seems to require "transport-accept" for acknowledging
   * the transport type. wjt confirms that this is apparently necessary for
   * incoming calls to work.
   */
  if (dialect == WOCKY_JINGLE_DIALECT_GTALK4)
    priv->gtalk4_event_id = g_idle_add (send_gtalk4_transport_accept, c);

  return;
}

static guint
new_share_channel (WockyJingleContent *c, const gchar *name)
{
  WockyJingleContentPrivate *priv = c->priv;
  WockyJingleTransportGoogle *gtrans = NULL;

  if (priv->transport &&
      WOCKY_IS_JINGLE_TRANSPORT_GOOGLE (priv->transport))
    {
      guint id = priv->last_share_channel_component_id + 1;

      gtrans = WOCKY_JINGLE_TRANSPORT_GOOGLE (priv->transport);

      if (!jingle_transport_google_set_component_name (gtrans, name, id))
        return 0;

      priv->last_share_channel_component_id++;

      DEBUG ("New Share channel '%s' with id : %d", name, id);

      g_signal_emit (c, signals[NEW_SHARE_CHANNEL], 0, name, id);

      return priv->last_share_channel_component_id;
    }
  return 0;
}

guint
wocky_jingle_content_create_share_channel (WockyJingleContent *self,
    const gchar *name)
{
  WockyJingleContentPrivate *priv = self->priv;
  WockyNode *sess_node, *channel_node;
  WockyStanza *msg = NULL;

  /* Send the info action before creating the channel, in case candidates need
     to be sent on the signal emit. It doesn't matter if the channel already
     exists anyways... */
  msg = wocky_jingle_session_new_message (self->session,
      WOCKY_JINGLE_ACTION_INFO, &sess_node);

  DEBUG ("Sending 'info' message to peer : channel %s", name);
  channel_node = wocky_node_add_child_ns (sess_node, "channel",
      priv->content_ns);
  wocky_node_set_attribute (channel_node, "name", name);

  wocky_jingle_session_send (self->session, msg);

  return new_share_channel (self, name);
}

void
wocky_jingle_content_send_complete (WockyJingleContent *self)
{
  WockyJingleContentPrivate *priv = self->priv;
  WockyNode *sess_node;
  WockyStanza *msg = NULL;

  msg = wocky_jingle_session_new_message (self->session,
      WOCKY_JINGLE_ACTION_INFO, &sess_node);

  DEBUG ("Sending 'info' message to peer : complete");
  wocky_node_add_child_ns (sess_node, "complete", priv->content_ns);

  wocky_jingle_session_send (self->session, msg);

}

void
wocky_jingle_content_parse_info (WockyJingleContent *c,
    WockyNode *content_node, GError **error)
{
  WockyNode *channel_node;
  WockyNode *complete_node;

  channel_node = wocky_node_get_child (content_node, "channel");
  complete_node = wocky_node_get_child (content_node, "complete");

  DEBUG ("parsing info message : %p - %p", channel_node, complete_node);
  if (channel_node)
    {
      const gchar *name;
      name = wocky_node_get_attribute (channel_node, "name");
      if (name != NULL)
        new_share_channel (c, name);
    }
  else if (complete_node)
    {
      g_signal_emit (c, signals[COMPLETED], 0);
    }

}

void
wocky_jingle_content_parse_accept (WockyJingleContent *c,
    WockyNode *content_node, gboolean google_mode, GError **error)
{
  WockyJingleContentPrivate *priv = c->priv;
  const gchar *senders;
  WockyNode *trans_node, *desc_node;
  WockyJingleDialect dialect = wocky_jingle_session_get_dialect (c->session);
  WockyJingleContentSenders newsenders;

  desc_node = wocky_node_get_child (content_node, "description");
  trans_node = wocky_node_get_child (content_node, "transport");
  senders = wocky_node_get_attribute (content_node, "senders");

  if (WOCKY_IS_JINGLE_MEDIA_RTP (c) &&
      WOCKY_JINGLE_DIALECT_IS_GOOGLE (dialect) && trans_node == NULL)
    {
      DEBUG ("no transport node, assuming GTalk3 dialect");
      /* gtalk lj0.3 assumes google-p2p transport */
      g_object_set (c->session, "dialect", WOCKY_JINGLE_DIALECT_GTALK3, NULL);
    }

  if (senders == NULL)
    newsenders = get_default_senders (c);
  else
    newsenders = parse_senders (senders);

  if (newsenders == WOCKY_JINGLE_CONTENT_SENDERS_NONE)
    {
      SET_BAD_REQ ("invalid content senders");
      return;
    }

  if (newsenders != priv->senders)
    {
      DEBUG ("changing senders from %s to %s", produce_senders (priv->senders),
          produce_senders (newsenders));
      priv->senders = newsenders;
      g_object_notify ((GObject *) c, "senders");
    }

  parse_description (c, desc_node, error);
  if (*error != NULL)
      return;

  priv->state = WOCKY_JINGLE_CONTENT_STATE_ACKNOWLEDGED;
  g_object_notify ((GObject *) c, "state");

  if (trans_node != NULL)
    {
      wocky_jingle_transport_iface_parse_candidates (priv->transport,
        trans_node, NULL);
    }
}

void
wocky_jingle_content_parse_description_info (WockyJingleContent *c,
    WockyNode *content_node, GError **error)
{
  WockyJingleContentPrivate *priv = c->priv;
  WockyNode *desc_node;
  desc_node = wocky_node_get_child (content_node, "description");
  if (desc_node == NULL)
    {
      SET_BAD_REQ ("invalid description-info action");
      return;
    }

  if (priv->created_by_us && priv->state < WOCKY_JINGLE_CONTENT_STATE_ACKNOWLEDGED)
    {
      /* The stream was created by us and the other side didn't acknowledge it
       * yet, thus we don't have their codec information, thus the
       * description-info isn't meaningful and can be ignored */
      DEBUG ("Ignoring description-info as we didn't receive the codecs yet");
      return;
    }

  parse_description (c, desc_node, error);
}


void
wocky_jingle_content_produce_node (WockyJingleContent *c,
    WockyNode *parent,
    gboolean include_description,
    gboolean include_transport,
    WockyNode **trans_node_out)
{
  WockyJingleContentPrivate *priv = c->priv;
  WockyNode *content_node, *trans_node;
  WockyJingleDialect dialect = wocky_jingle_session_get_dialect (c->session);
  void (*produce_desc)(WockyJingleContent *, WockyNode *) =
    WOCKY_JINGLE_CONTENT_GET_CLASS (c)->produce_description;

  if ((dialect == WOCKY_JINGLE_DIALECT_GTALK3) ||
      (dialect == WOCKY_JINGLE_DIALECT_GTALK4))
    {
      content_node = parent;
    }
  else
    {
      content_node = wocky_node_add_child (parent, "content");
      wocky_node_set_attributes (content_node,
          "name", priv->name,
          "senders", produce_senders (priv->senders),
          NULL);

      if (wocky_jingle_content_creator_is_initiator (c))
        wocky_node_set_attribute (content_node, "creator", "initiator");
      else
        wocky_node_set_attribute (content_node, "creator", "responder");
    }

  if (include_description)
    produce_desc (c, content_node);

  if (include_transport)
    {
      if (dialect == WOCKY_JINGLE_DIALECT_GTALK3)
        {
          /* GTalk 03 doesn't use a transport, but assumes gtalk-p2p */
          trans_node = parent;
        }
      else
        {
          trans_node = wocky_node_add_child_ns (content_node, "transport",
              priv->transport_ns);
        }

      if (trans_node_out != NULL)
        *trans_node_out = trans_node;
    }
}

void
wocky_jingle_content_update_senders (WockyJingleContent *c,
    WockyNode *content_node, GError **error)
{
  WockyJingleContentPrivate *priv = c->priv;
  WockyJingleContentSenders senders;

  senders = parse_senders (wocky_node_get_attribute (content_node, "senders"));

  if (senders == WOCKY_JINGLE_CONTENT_SENDERS_NONE)
    {
      SET_BAD_REQ ("invalid content senders in stream");
      return;
    }

  priv->senders = senders;
  g_object_notify ((GObject *) c, "senders");
}

void
wocky_jingle_content_parse_transport_info (WockyJingleContent *self,
  WockyNode *trans_node, GError **error)
{
  WockyJingleContentPrivate *priv = self->priv;

  wocky_jingle_transport_iface_parse_candidates (priv->transport, trans_node, error);
}

/* Takes in a list of slice-allocated WockyJingleCandidate structs */
void
wocky_jingle_content_add_candidates (WockyJingleContent *self, GList *li)
{
  WockyJingleContentPrivate *priv = self->priv;

  DEBUG ("called content: %s created_by_us: %d", priv->name,
      priv->created_by_us);

  if (li == NULL)
    return;

  wocky_jingle_transport_iface_new_local_candidates (priv->transport, li);

  if (!priv->have_local_candidates)
    {
      priv->have_local_candidates = TRUE;
      /* Maybe we were waiting for at least one candidate? */
      _maybe_ready (self);
    }

  /* If the content exists on the wire, let the transport send this candidate
   * if it wants to.
   */
  if (priv->state > WOCKY_JINGLE_CONTENT_STATE_EMPTY)
    wocky_jingle_transport_iface_send_candidates (priv->transport, FALSE);
}

/* Returns whether the content is ready to be signalled (initiated, for local
 * streams, or acknowledged, for remote streams. */
gboolean
wocky_jingle_content_is_ready (WockyJingleContent *self)
{
  WockyJingleContentPrivate *priv = self->priv;

  if (priv->created_by_us)
    {
      /* If it's created by us, media ready, not signalled, and we have
       * at least one local candidate, it's ready to be added. */
      if (priv->media_ready && priv->state == WOCKY_JINGLE_CONTENT_STATE_EMPTY &&
          (!WOCKY_IS_JINGLE_MEDIA_RTP (self) || priv->have_local_candidates))
        return TRUE;
    }
  else
    {
      /* If it's created by peer, media and transports ready,
       * and not acknowledged yet, it's ready for acceptance. */
      if (priv->media_ready && priv->state == WOCKY_JINGLE_CONTENT_STATE_NEW &&
          (!WOCKY_IS_JINGLE_MEDIA_RTP (self) ||
              wocky_jingle_transport_iface_can_accept (priv->transport)))
        return TRUE;
    }

  return FALSE;
}

static void
send_content_add_or_accept (WockyJingleContent *self)
{
  WockyJingleContentPrivate *priv = self->priv;
  WockyStanza *msg;
  WockyNode *sess_node, *transport_node;
  WockyJingleAction action;
  WockyJingleContentState new_state = WOCKY_JINGLE_CONTENT_STATE_EMPTY;

  g_assert (wocky_jingle_content_is_ready (self));

  if (priv->created_by_us)
    {
      /* TODO: set a timer for acknowledgement */
      action = WOCKY_JINGLE_ACTION_CONTENT_ADD;
      new_state = WOCKY_JINGLE_CONTENT_STATE_SENT;
    }
  else
    {
      action = WOCKY_JINGLE_ACTION_CONTENT_ACCEPT;
      new_state = WOCKY_JINGLE_CONTENT_STATE_ACKNOWLEDGED;
    }

  msg = wocky_jingle_session_new_message (self->session,
      action, &sess_node);
  wocky_jingle_content_produce_node (self, sess_node, TRUE, TRUE,
      &transport_node);
  wocky_jingle_transport_iface_inject_candidates (priv->transport,
      transport_node);
  wocky_jingle_session_send (self->session, msg);

  priv->state = new_state;
  g_object_notify (G_OBJECT (self), "state");
}

static void
_maybe_ready (WockyJingleContent *self)
{
  WockyJingleContentPrivate *priv = self->priv;
  WockyJingleState state;

  if (!wocky_jingle_content_is_ready (self))
      return;

  /* If content disposition is session and session
   * is not yet acknowledged/active, we signall
   * the readiness to the session and let it take
   * care of it. Otherwise, we can deal with it
   * ourselves. */

  g_object_get (self->session, "state", &state, NULL);

  if (!wocky_strdiff (priv->disposition, "session") &&
      (state < WOCKY_JINGLE_STATE_PENDING_ACCEPT_SENT))
    {
      /* Notify the session that we're ready for
       * session-initiate/session-accept */
      g_signal_emit (self, signals[READY], 0);
    }
  else
    {
      if (state >= WOCKY_JINGLE_STATE_PENDING_INITIATE_SENT)
        {
          send_content_add_or_accept (self);

          /* if neccessary, transmit the candidates */
          wocky_jingle_transport_iface_send_candidates (priv->transport,
              FALSE);
        }
      else
        {
          /* non session-disposition content ready without session
           * being initiated at all? */
          DEBUG ("session not initiated yet, ignoring non-session ready content");
          return;
        }
    }
}

void
wocky_jingle_content_maybe_send_description (WockyJingleContent *self)
{
  WockyJingleContentPrivate *priv = self->priv;

  /* If we didn't send the content yet there is no reason to send a
   * description-info to update it */
  if (priv->state < WOCKY_JINGLE_CONTENT_STATE_SENT)
    return;

  if (wocky_jingle_session_defines_action (self->session,
          WOCKY_JINGLE_ACTION_DESCRIPTION_INFO))
    {
      WockyNode *sess_node;
      WockyStanza *msg = wocky_jingle_session_new_message (self->session,
          WOCKY_JINGLE_ACTION_DESCRIPTION_INFO, &sess_node);

      wocky_jingle_content_produce_node (self, sess_node, TRUE, FALSE, NULL);
      wocky_jingle_session_send (self->session, msg);
    }
  else
    {
      DEBUG ("not sending description-info, speaking an old dialect");
    }
}


/* Used when session-initiate is sent (so all initial contents transmit their
 * candidates), and when we detect gtalk3 after we've transmitted some
 * candidates. */
void
wocky_jingle_content_retransmit_candidates (WockyJingleContent *self,
    gboolean all)
{
  wocky_jingle_transport_iface_send_candidates (self->priv->transport, all);
}

void
wocky_jingle_content_inject_candidates (WockyJingleContent *self,
    WockyNode *transport_node)
{
  wocky_jingle_transport_iface_inject_candidates (self->priv->transport,
      transport_node);
}


/* Called by a subclass when the media is ready (e.g. we got local codecs) */
void
_wocky_jingle_content_set_media_ready (WockyJingleContent *self)
{
  WockyJingleContentPrivate *priv = self->priv;

  DEBUG ("media ready on content: %s created_by_us: %d", priv->name,
      priv->created_by_us);

  priv->media_ready = TRUE;

  _maybe_ready (self);
}

void
wocky_jingle_content_set_transport_state (WockyJingleContent *self,
    WockyJingleTransportState state)
{
  WockyJingleContentPrivate *priv = self->priv;

  g_object_set (priv->transport, "state", state, NULL);

  _maybe_ready (self);
}

GList *
wocky_jingle_content_get_remote_candidates (WockyJingleContent *c)
{
  WockyJingleContentPrivate *priv = c->priv;

  return wocky_jingle_transport_iface_get_remote_candidates (priv->transport);
}

GList *
wocky_jingle_content_get_local_candidates (WockyJingleContent *c)
{
  WockyJingleContentPrivate *priv = c->priv;

  return wocky_jingle_transport_iface_get_local_candidates (priv->transport);
}

gboolean
wocky_jingle_content_get_credentials (WockyJingleContent *c,
    gchar **ufrag, gchar **pwd)
{
  WockyJingleContentPrivate *priv = c->priv;

  return jingle_transport_get_credentials (priv->transport, ufrag, pwd);
}

gboolean
wocky_jingle_content_change_direction (WockyJingleContent *c,
    WockyJingleContentSenders senders)
{
  WockyJingleContentPrivate *priv = c->priv;
  WockyStanza *msg;
  WockyNode *sess_node;
  WockyJingleDialect dialect = wocky_jingle_session_get_dialect (c->session);

  if (senders == priv->senders)
    return TRUE;

  priv->senders = senders;
  g_object_notify (G_OBJECT (c), "senders");

  if (WOCKY_JINGLE_DIALECT_IS_GOOGLE (dialect))
    {
      DEBUG ("ignoring direction change request for GTalk stream");
      return FALSE;
    }

  if (priv->state >= WOCKY_JINGLE_CONTENT_STATE_SENT)
    {
      msg = wocky_jingle_session_new_message (c->session,
          WOCKY_JINGLE_ACTION_CONTENT_MODIFY, &sess_node);
      wocky_jingle_content_produce_node (c, sess_node, FALSE, FALSE, NULL);
      wocky_jingle_session_send (c->session, msg);
    }

  /* FIXME: actually check whether remote end accepts our content-modify */
  return TRUE;
}

static void
_on_remove_reply (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyJingleContent *c = WOCKY_JINGLE_CONTENT (user_data);
  WockyJingleContentPrivate *priv = c->priv;

  g_assert (priv->state == WOCKY_JINGLE_CONTENT_STATE_REMOVING);

  DEBUG ("%p", c);

  /* Everything holding a reference to a content should drop it after receiving
   * 'removed'.
   */
  g_signal_emit (c, signals[REMOVED], 0);
  g_object_unref (c);
}

static void
_content_remove (WockyJingleContent *c,
    gboolean signal_peer,
    WockyJingleReason reason)
{
  WockyJingleContentPrivate *priv = c->priv;
  WockyStanza *msg;
  WockyNode *sess_node;

  DEBUG ("called for %p (%s)", c, priv->name);

  /* If we were already signalled and removal is not a side-effect of
   * something else (sesssion termination, or removal by peer),
   * we have to signal removal to the peer. */
  if (signal_peer && (priv->state != WOCKY_JINGLE_CONTENT_STATE_EMPTY))
    {
      if (priv->state == WOCKY_JINGLE_CONTENT_STATE_REMOVING)
        {
          DEBUG ("ignoring request to remove content which is already being removed");
          return;
        }

      priv->state = WOCKY_JINGLE_CONTENT_STATE_REMOVING;
      g_object_notify ((GObject *) c, "state");

      msg = wocky_jingle_session_new_message (c->session,
          reason == WOCKY_JINGLE_REASON_UNKNOWN ?
          WOCKY_JINGLE_ACTION_CONTENT_REMOVE : WOCKY_JINGLE_ACTION_CONTENT_REJECT,
          &sess_node);

      if (reason != WOCKY_JINGLE_REASON_UNKNOWN)
        {
          WockyNode *reason_node = wocky_node_add_child_with_content (sess_node,
              "reason", NULL);
          wocky_node_add_child_with_content (reason_node,
              wocky_jingle_session_get_reason_name (reason), NULL);
        }

      wocky_jingle_content_produce_node (c, sess_node, FALSE, FALSE, NULL);
      wocky_porter_send_iq_async (wocky_jingle_session_get_porter (c->session),
          msg, NULL, _on_remove_reply, g_object_ref (c));
      g_object_unref (msg);
    }
  else
    {
      DEBUG ("signalling removed with %u refs", G_OBJECT (c)->ref_count);
      /* Everything holding a reference to a content should drop it after receiving
       * 'removed'.
       */
      g_signal_emit (c, signals[REMOVED], 0);
    }
}

void
wocky_jingle_content_remove (WockyJingleContent *c,
    gboolean signal_peer)
{
  _content_remove (c, signal_peer, WOCKY_JINGLE_REASON_UNKNOWN);
}

void
wocky_jingle_content_reject (WockyJingleContent *c,
    WockyJingleReason reason)
{
  _content_remove (c, TRUE, reason);
}

gboolean
wocky_jingle_content_is_created_by_us (WockyJingleContent *c)
{
  return c->priv->created_by_us;
}

gboolean
wocky_jingle_content_creator_is_initiator (WockyJingleContent *c)
{
  gboolean session_created_by_us;

  g_object_get (c->session, "local-initiator", &session_created_by_us, NULL);

  return (c->priv->created_by_us == session_created_by_us);
}

const gchar *
wocky_jingle_content_get_name (WockyJingleContent *self)
{
  return self->priv->name;
}

const gchar *
wocky_jingle_content_get_ns (WockyJingleContent *self)
{
  return self->priv->content_ns;
}

const gchar *
wocky_jingle_content_get_transport_ns (WockyJingleContent *self)
{
  return self->priv->transport_ns;
}

const gchar *
wocky_jingle_content_get_disposition (WockyJingleContent *self)
{
  return self->priv->disposition;
}

WockyJingleTransportType
wocky_jingle_content_get_transport_type (WockyJingleContent *c)
{
  return wocky_jingle_transport_iface_get_transport_type (c->priv->transport);
}

static gboolean
jingle_content_has_direction (WockyJingleContent *self,
  gboolean sending)
{
  WockyJingleContentPrivate *priv = self->priv;
  gboolean initiated_by_us;

  g_object_get (self->session, "local-initiator",
    &initiated_by_us, NULL);

  switch (priv->senders)
    {
      case WOCKY_JINGLE_CONTENT_SENDERS_BOTH:
        return TRUE;
      case WOCKY_JINGLE_CONTENT_SENDERS_NONE:
        return FALSE;
      case WOCKY_JINGLE_CONTENT_SENDERS_INITIATOR:
        return sending ? initiated_by_us : !initiated_by_us;
      case WOCKY_JINGLE_CONTENT_SENDERS_RESPONDER:
        return sending ? !initiated_by_us : initiated_by_us;
    }

  return FALSE;
}

gboolean
wocky_jingle_content_sending (WockyJingleContent *self)
{
  return jingle_content_has_direction (self, TRUE);
}

gboolean
wocky_jingle_content_receiving (WockyJingleContent *self)
{
  return jingle_content_has_direction (self, FALSE);
}

void
wocky_jingle_content_set_sending (WockyJingleContent *self,
  gboolean send)
{
  WockyJingleContentPrivate *priv = self->priv;
  WockyJingleContentSenders senders;
  gboolean initiated_by_us;

  if (send == wocky_jingle_content_sending (self))
    return;

  g_object_get (self->session, "local-initiator",
    &initiated_by_us, NULL);

  if (send)
    {
      if (priv->senders == WOCKY_JINGLE_CONTENT_SENDERS_NONE)
        senders = (initiated_by_us ? WOCKY_JINGLE_CONTENT_SENDERS_INITIATOR :
          WOCKY_JINGLE_CONTENT_SENDERS_RESPONDER);
      else
        senders = WOCKY_JINGLE_CONTENT_SENDERS_BOTH;
    }
  else
    {
      if (priv->senders == WOCKY_JINGLE_CONTENT_SENDERS_BOTH)
        senders = (initiated_by_us ? WOCKY_JINGLE_CONTENT_SENDERS_RESPONDER :
          WOCKY_JINGLE_CONTENT_SENDERS_INITIATOR);
      else
        senders = WOCKY_JINGLE_CONTENT_SENDERS_NONE;
    }

  if (senders == WOCKY_JINGLE_CONTENT_SENDERS_NONE)
    wocky_jingle_content_remove (self, TRUE);
  else
    wocky_jingle_content_change_direction (self, senders);
}


void
wocky_jingle_content_request_receiving (WockyJingleContent *self,
  gboolean receive)
{
  WockyJingleContentPrivate *priv = self->priv;
  WockyJingleContentSenders senders;
  gboolean initiated_by_us;

  if (receive == wocky_jingle_content_receiving (self))
    return;

  g_object_get (self->session, "local-initiator",
    &initiated_by_us, NULL);

  if (receive)
    {
      if (priv->senders == WOCKY_JINGLE_CONTENT_SENDERS_NONE)
        senders = (initiated_by_us ? WOCKY_JINGLE_CONTENT_SENDERS_RESPONDER :
            WOCKY_JINGLE_CONTENT_SENDERS_INITIATOR);
      else
        senders = WOCKY_JINGLE_CONTENT_SENDERS_BOTH;
    }
  else
    {
      if (priv->senders == WOCKY_JINGLE_CONTENT_SENDERS_BOTH)
        senders = (initiated_by_us ? WOCKY_JINGLE_CONTENT_SENDERS_INITIATOR :
            WOCKY_JINGLE_CONTENT_SENDERS_RESPONDER);
      else
        senders = WOCKY_JINGLE_CONTENT_SENDERS_NONE;
    }


  if (senders == WOCKY_JINGLE_CONTENT_SENDERS_NONE)
    wocky_jingle_content_remove (self, TRUE);
  else
    wocky_jingle_content_change_direction (self, senders);
}
