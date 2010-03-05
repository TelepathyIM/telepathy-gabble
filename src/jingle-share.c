/*
 * jingle-share.c - Source for GabbleJingleShare
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
  PROP_FILENAME,
  PROP_FILESIZE,
  LAST_PROPERTY
};

struct _GabbleJingleSharePrivate
{
  gboolean dispose_has_run;

  GabbleJingleShareManifest *manifest;
  gchar *filename;
  guint64 filesize;
};


static gchar *
generate_temp_url (void)
{
  gchar buf[sizeof (guint32) * 2];
  guint32 *uint_buf = (guint32 *) buf;
  guint i;

  for (i = 0; i < sizeof (buf); i++)
    buf[i] = g_random_int_range (0, 256);

  return g_strdup_printf ("/temporary/%x%x/", uint_buf[0], uint_buf[1]);
}

static void
free_manifest (GabbleJingleShare *self)
{
  GList * i;

  if (self->priv->manifest)
    {
      for (i = self->priv->manifest->entries; i; i = i->next)
        {
          GabbleJingleShareManifestEntry *item = i->data;
          g_free (item->name);
          g_slice_free (GabbleJingleShareManifestEntry, item);
        }
      g_list_free (self->priv->manifest->entries);

      g_free (self->priv->manifest->source_url);
      g_free (self->priv->manifest->preview_url);

      g_slice_free (GabbleJingleShareManifest, self->priv->manifest);
      self->priv->manifest = NULL;
    }
}

static void
gen_manifest (GabbleJingleShare *self)
{
  if (self->priv->manifest == NULL)
    {
      GabbleJingleShareManifestEntry *m = NULL;

      self->priv->manifest = g_slice_new0 (GabbleJingleShareManifest);
      self->priv->manifest->source_url = generate_temp_url ();
      self->priv->manifest->preview_url = generate_temp_url ();

      if (self->priv->filename != NULL)
        {
          m = g_slice_new0 (GabbleJingleShareManifestEntry);
          m->name = g_strdup (self->priv->filename);
          m->size = self->priv->filesize;
          self->priv->manifest->entries = g_list_prepend (NULL, m);
        }
    }
}

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
  GabbleJingleShare *self = GABBLE_JINGLE_SHARE (object);
  GabbleJingleSharePrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  g_free (priv->filename);
  priv->filename = NULL;

  free_manifest (self);

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
  GabbleJingleShare *self = GABBLE_JINGLE_SHARE (object);
  GabbleJingleSharePrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_MEDIA_TYPE:
        g_value_set_uint (value, JINGLE_MEDIA_TYPE_FILE);
        break;
      case PROP_FILENAME:
        g_value_set_string (value, priv->filename);
        break;
      case PROP_FILESIZE:
        g_value_set_uint64 (value, priv->filesize);
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
  GabbleJingleShare *self = GABBLE_JINGLE_SHARE (object);
  GabbleJingleSharePrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_MEDIA_TYPE:
        break;
      case PROP_FILENAME:
        g_free (priv->filename);
        priv->filename = g_value_dup_string (value);
        free_manifest (self);
        /* simulate a media_ready when we know our own filename */
        _gabble_jingle_content_set_media_ready (GABBLE_JINGLE_CONTENT (self));
        break;
      case PROP_FILESIZE:
        priv->filesize = g_value_get_uint64 (value);
        free_manifest (self);
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
          JINGLE_MEDIA_TYPE_NONE, G_MAXUINT32, JINGLE_MEDIA_TYPE_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FILENAME,
      g_param_spec_string ("filename", "file name",
          "The name of the file",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FILESIZE,
      g_param_spec_uint64 ("filesize", "file size",
          "The size of the file",
          0, G_MAXUINT64, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

static void
parse_description (GabbleJingleContent *content,
    LmMessageNode *desc_node, GError **error)
{
  GabbleJingleShare *self = GABBLE_JINGLE_SHARE (content);
  GabbleJingleSharePrivate *priv = self->priv;
  NodeIter i;
  LmMessageNode *manifest_node = NULL;
  LmMessageNode *protocol_node = NULL;
  LmMessageNode *http_node = NULL;

  DEBUG ("parse description called");

  if (priv->manifest != NULL)
    {
      DEBUG ("Not parsing description, we already have a manifest");
      return;
    }

  manifest_node = lm_message_node_get_child (desc_node, "manifest");
  protocol_node = lm_message_node_get_child (desc_node, "protocol");
  if (protocol_node != NULL)
    http_node = lm_message_node_get_child (protocol_node, "http");

  free_manifest (self);
  priv->manifest = g_slice_new0 (GabbleJingleShareManifest);

  /* Build the manifest */
  for (i = node_iter (manifest_node); i; i = node_iter_next (i))
    {
      LmMessageNode *node = node_iter_data (i);
      LmMessageNode *name = NULL;
      LmMessageNode *image = NULL;
      gboolean folder;
      const gchar *size;
      GabbleJingleShareManifestEntry *m = NULL;

      if (!tp_strdiff (lm_message_node_get_name (node), "folder"))
        folder = TRUE;
      else if (!tp_strdiff (lm_message_node_get_name (node), "file"))
        folder = FALSE;
      else
        continue;

      name = lm_message_node_get_child (node, "name");
      if (name == NULL)
        continue;

      m = g_slice_new0 (GabbleJingleShareManifestEntry);
      m->folder = folder;
      m->name = g_strdup (lm_message_node_get_value (name));

      size = lm_message_node_get_attribute (node, "size");
      if (size)
        m->size = strtoull (size, NULL, 10);

      image = lm_message_node_get_child (node, "image");
      if (image)
        {
          const gchar *width;
          const gchar *height;

          m->image = TRUE;

          width = lm_message_node_get_attribute (image, "width");
          if (width)
            m->image_width = atoi (width);

          height =lm_message_node_get_attribute (image, "height");
          if (height)
            m->image_height = atoi (height);
        }
      priv->manifest->entries = g_list_prepend (priv->manifest->entries, m);
    }

  /* Get the source and preview url paths from the protocol/http node */
  if (http_node != NULL)
    {
      /* clear the previously set values */
      for (i = node_iter (http_node); i; i = node_iter_next (i))
        {
          LmMessageNode *node = node_iter_data (i);
          const gchar *name;

          if (tp_strdiff (lm_message_node_get_name (node), "url"))
            continue;

          name = lm_message_node_get_attribute (node, "name");
          if (name == NULL)
            continue;

          if (!tp_strdiff (name, "source-path"))
            {
              const gchar *url = lm_message_node_get_value (node);
              priv->manifest->source_url = g_strdup (url);
            }

          if (!tp_strdiff (name, "preview-path"))
            {
              const gchar *url = lm_message_node_get_value (node);
              priv->manifest->preview_url = g_strdup (url);
            }
        }
    }

  /* Build the filename/filesize property values based on the new manifest */
  g_free (priv->filename);
  priv->filename = NULL;
  priv->filesize = 0;

  if (g_list_length (priv->manifest->entries) > 0)
    {
      if (g_list_length (priv->manifest->entries) == 1)
        {
          GabbleJingleShareManifestEntry *m = priv->manifest->entries->data;
          if (m->folder)
            priv->filename = g_strdup_printf ("%s.tar", m->name);
          else
            priv->filename = g_strdup (m->name);

          priv->filesize = m->size;
        }
      else
        {
          GList *li;
          gchar *temp;
          priv->filename = g_strdup ("");
          for (li = priv->manifest->entries; li; li = li->next)
            {
              GabbleJingleShareManifestEntry *m = li->data;

              temp = priv->filename;
              priv->filename = g_strdup_printf ("%s%s%s%s",  temp, m->name,
                  m->folder? ".tar":"", li->next == NULL? "": "-");
              g_free (temp);

              priv->filesize += m->size;
            }
          temp = priv->filename;
          priv->filename = g_strdup_printf ("%s.tar", temp);
          g_free (temp);
        }
    }
  _gabble_jingle_content_set_media_ready (content);
}

static void
produce_description (GabbleJingleContent *content, LmMessageNode *content_node)
{
  GabbleJingleShare *self = GABBLE_JINGLE_SHARE (content);
  GabbleJingleSharePrivate *priv = self->priv;
  GList *i;

  LmMessageNode *desc_node;
  LmMessageNode *manifest_node;
  LmMessageNode *protocol_node;
  LmMessageNode *http_node;
  LmMessageNode *url_node;

  DEBUG ("produce description called");

  gen_manifest (self);

  desc_node = lm_message_node_add_child (content_node, "description", NULL);

  lm_message_node_set_attribute (desc_node, "xmlns", NS_GOOGLE_SESSION_SHARE);

  manifest_node = lm_message_node_add_child (desc_node, "manifest", NULL);

  for (i = priv->manifest->entries; i; i = i->next)
    {
      GabbleJingleShareManifestEntry *m = i->data;
      LmMessageNode *file_node;
      LmMessageNode *image_node;
      gchar *size_str, *width_str, *height_str;

      if (m->folder)
        file_node = lm_message_node_add_child (manifest_node, "folder", NULL);
      else
        file_node = lm_message_node_add_child (manifest_node, "file", NULL);

      if (m->size > 0)
        {
          size_str = g_strdup_printf ("%llu", m->size);
          lm_message_node_set_attribute (file_node, "size", size_str);
          g_free (size_str);
        }
      lm_message_node_add_child (file_node, "name", m->name);

      if (m->image &&
          (m->image_width > 0 || m->image_height > 0))
        {
          image_node = lm_message_node_add_child (file_node, "image", NULL);
          if (m->image_width > 0)
            {
              width_str = g_strdup_printf ("%d", m->image_width);
              lm_message_node_set_attribute (image_node, "width", width_str);
              g_free (width_str);
            }

          if (m->image_height > 0)
            {
              height_str = g_strdup_printf ("%d", m->image_height);
              lm_message_node_set_attribute (image_node, "height", height_str);
              g_free (height_str);
            }
        }
    }

  protocol_node = lm_message_node_add_child (desc_node, "protocol", NULL);
  http_node = lm_message_node_add_child (protocol_node, "http", NULL);
  url_node = lm_message_node_add_child (http_node, "url",
      priv->manifest->source_url);
  lm_message_node_set_attribute (url_node, "name", "source-path");
  url_node = lm_message_node_add_child (http_node, "url",
      priv->manifest->preview_url);
  lm_message_node_set_attribute (url_node, "name", "preview-path");

}

GabbleJingleShareManifest *
gabble_jingle_share_get_manifest (GabbleJingleShare *self)
{
  gen_manifest (self);
  return self->priv->manifest;
}

void
jingle_share_register (GabbleJingleFactory *factory)
{
  /* GTalk video call namespace */
  gabble_jingle_factory_register_content_type (factory,
      NS_GOOGLE_SESSION_SHARE,
      GABBLE_TYPE_JINGLE_SHARE);
}
