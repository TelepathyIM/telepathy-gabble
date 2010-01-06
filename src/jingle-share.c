/*
 * jingle-share.c - Source for GabbleJingleShare
 *
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

/* Share content type deals with file sharing content, ie. file transfers. It
 * Google's jingle variants (libjingle 0.3/0.4). */

#include "jingle-share.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_SHARE

#include "connection.h"
#include "debug.h"
#include "jingle-content.h"
#include "jingle-factory.h"
#include "jingle-session.h"
#include "namespaces.h"
#include "util.h"

G_DEFINE_TYPE (GabbleJingleShare,
    gabble_jingle_share, GABBLE_TYPE_JINGLE_CONTENT);

/* properties */
enum
{
  PROP_MEDIA_TYPE = 1,
  LAST_PROPERTY
};

struct _GabbleJingleSharePrivate
{
  gboolean dispose_has_run;
};

static void
gabble_jingle_share_init (GabbleJingleShare *obj)
{
  GabbleJingleSharePrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_JINGLE_SHARE,
         GabbleJingleSharePrivate);
  DEBUG ("jingle share init called");
  obj->priv = priv;
  priv->dispose_has_run = FALSE;
}


static void
gabble_jingle_share_dispose (GObject *object)
{
  GabbleJingleShare *trans = GABBLE_JINGLE_SHARE (object);
  GabbleJingleSharePrivate *priv = trans->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (gabble_jingle_share_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_jingle_share_parent_class)->dispose (object);
}


static void parse_description (GabbleJingleContent *content,
    LmMessageNode *desc_node, GError **error);
static void produce_description (GabbleJingleContent *obj,
    LmMessageNode *content_node);


static void
gabble_jingle_share_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
  switch (property_id) {
    case PROP_MEDIA_TYPE:
      g_value_set_uint (value, JINGLE_MEDIA_TYPE_FILE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_jingle_share_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
  switch (property_id) {
    case PROP_MEDIA_TYPE:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_jingle_share_class_init (GabbleJingleShareClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GabbleJingleContentClass *content_class = GABBLE_JINGLE_CONTENT_CLASS (cls);

  g_type_class_add_private (cls, sizeof (GabbleJingleSharePrivate));

  object_class->get_property = gabble_jingle_share_get_property;
  object_class->set_property = gabble_jingle_share_set_property;
  object_class->dispose = gabble_jingle_share_dispose;

  content_class->parse_description = parse_description;
  content_class->produce_description = produce_description;

  /* This property is here only because jingle-session sets the media-type
     when constructing the object.. */
  g_object_class_install_property (object_class, PROP_MEDIA_TYPE,
      g_param_spec_uint ("media-type", "media type",
          "Media type.",
          JINGLE_MEDIA_TYPE_NONE, G_MAXUINT32, JINGLE_MEDIA_TYPE_FILE,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

static void
parse_description (GabbleJingleContent *content,
    LmMessageNode *desc_node, GError **error)
{
  //GabbleJingleShare *self = GABBLE_JINGLE_SHARE (content);
  //GabbleJingleSharePrivate *priv = self->priv;
  //JingleDialect dialect = gabble_jingle_session_get_dialect (content->session);

  DEBUG ("node: %s", desc_node->name);
}

static void
produce_description (GabbleJingleContent *obj, LmMessageNode *content_node)
{
  //GabbleJingleShare *self = GABBLE_JINGLE_SHARE (obj);
  //GabbleJingleSharePrivate *priv = desc->priv;
  //JingleDialect dialect = gabble_jingle_session_get_dialect (obj->session);

  DEBUG ("produce description called");
}

void
jingle_share_register (GabbleJingleFactory *factory)
{
  /* GTalk video call namespace */
  gabble_jingle_factory_register_content_type (factory,
      NS_GOOGLE_SESSION_SHARE,
      GABBLE_TYPE_JINGLE_SHARE);
}
