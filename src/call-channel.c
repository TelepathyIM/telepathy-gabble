/*
 * gabble-call-channel.c - Source for GabbleCallChannel
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

#include <gio/gio.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/gtypes.h>

#include <extensions/extensions.h>

#include "util.h"
#include "call-channel.h"
#include "call-content.h"

#include "connection.h"
#include "jingle-session.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"

static void async_initable_iface_init (GAsyncInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE(GabbleCallChannel, gabble_call_channel,
  GABBLE_TYPE_BASE_CALL_CHANNEL,
  G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init);
);

/* properties */
enum
{
  LAST_PROPERTY
};


/* private structure */
struct _GabbleCallChannelPrivate
{
  gboolean dispose_has_run;
};

static void
gabble_call_channel_constructed (GObject *obj)
{
  if (G_OBJECT_CLASS (gabble_call_channel_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gabble_call_channel_parent_class)->constructed (obj);
}

static void
gabble_call_channel_init (GabbleCallChannel *self)
{
  GabbleCallChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CALL_CHANNEL, GabbleCallChannelPrivate);

  self->priv = priv;
}

static void gabble_call_channel_dispose (GObject *object);
static void gabble_call_channel_finalize (GObject *object);

static void
gabble_call_channel_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  switch (property_id)
    {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gabble_call_channel_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  switch (property_id)
    {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}

static void
gabble_call_channel_class_init (
    GabbleCallChannelClass *gabble_call_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_call_channel_class);
  GabbleBaseCallChannelClass *base_call_class =
    GABBLE_BASE_CALL_CHANNEL_CLASS (gabble_call_channel_class);

  g_type_class_add_private (gabble_call_channel_class,
      sizeof (GabbleCallChannelPrivate));

  object_class->constructed = gabble_call_channel_constructed;

  object_class->get_property = gabble_call_channel_get_property;
  object_class->set_property = gabble_call_channel_set_property;

  object_class->dispose = gabble_call_channel_dispose;
  object_class->finalize = gabble_call_channel_finalize;

  base_call_class->handle_type = TP_HANDLE_TYPE_CONTACT;
}

void
gabble_call_channel_dispose (GObject *object)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (object);
  GabbleCallChannelPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (gabble_call_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_call_channel_parent_class)->dispose (object);
}

void
gabble_call_channel_finalize (GObject *object)
{
  G_OBJECT_CLASS (gabble_call_channel_parent_class)->finalize (object);
}

static void
call_channel_init_async (GAsyncInitable *initable,
  int priority,
  GCancellable *cancellable,
  GAsyncReadyCallback callback,
  gpointer user_data)
{
  GabbleCallChannel *self = GABBLE_CALL_CHANNEL (initable);
  GabbleBaseCallChannel *base = GABBLE_BASE_CALL_CHANNEL (self);
  GSimpleAsyncResult *result;

  result = g_simple_async_result_new (G_OBJECT (self),
    callback, user_data, NULL);

  if (gabble_base_call_channel_registered (base))
    goto out;

  if (gabble_base_call_channel_get_session (base) == NULL)
    {
      const gchar *resource;
      JingleDialect dialect;
      GabbleJingleSession *session;
      const gchar *transport;

      /* FIXME might need to wait on capabilities, also don't need transport
       * and dialect already */
      if (!jingle_pick_best_resource (base->conn,
        base->target, base->initial_audio, base->initial_video,
        &transport, &dialect, &resource))
        {
          g_simple_async_result_set_error (result, TP_ERRORS,
            TP_ERROR_NOT_CAPABLE,
            "member does not have the desired audio/video capabilities");
          goto out;
        }

      gabble_base_call_channel_set_transport (base, transport);

      session = gabble_jingle_factory_create_session (
          base->conn->jingle_factory, base->target, resource, FALSE);
      g_object_set (session, "dialect", dialect, NULL);

      gabble_base_call_channel_set_session (base, session);
      g_object_unref (session);

      /* Setup the session and the initial contents */
      if (base->initial_audio)
        gabble_base_call_channel_create_content (base, "Audio",
          JINGLE_MEDIA_TYPE_AUDIO,
          GABBLE_CALL_CONTENT_DISPOSITION_INITIAL, NULL);

      if (base->initial_video)
        gabble_base_call_channel_create_content (base, "Video",
          JINGLE_MEDIA_TYPE_VIDEO,
          GABBLE_CALL_CONTENT_DISPOSITION_INITIAL, NULL);
    }

  gabble_base_call_channel_register (base);

out:
  g_simple_async_result_complete_in_idle (result);
  g_object_unref (result);
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
  iface->init_async = call_channel_init_async;
}
