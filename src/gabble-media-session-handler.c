/*
 * gabble-media-session-handler.c - Source for GabbleMediaSessionHandler
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "gabble-media-session-handler.h"
#include "gabble-media-session-handler-signals-marshal.h"

#include "gabble-media-session-handler-glue.h"

G_DEFINE_TYPE(GabbleMediaSessionHandler, gabble_media_session_handler, G_TYPE_OBJECT)

/* signal enum */
enum
{
    NEW_MEDIA_STREAM_HANDLER,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _GabbleMediaSessionHandlerPrivate GabbleMediaSessionHandlerPrivate;

struct _GabbleMediaSessionHandlerPrivate
{
  gboolean dispose_has_run;
};

#define GABBLE_MEDIA_SESSION_HANDLER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MEDIA_SESSION_HANDLER, GabbleMediaSessionHandlerPrivate))

static void
gabble_media_session_handler_init (GabbleMediaSessionHandler *obj)
{
  GabbleMediaSessionHandlerPrivate *priv = GABBLE_MEDIA_SESSION_HANDLER_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
}

static void gabble_media_session_handler_dispose (GObject *object);
static void gabble_media_session_handler_finalize (GObject *object);

static void
gabble_media_session_handler_class_init (GabbleMediaSessionHandlerClass *gabble_media_session_handler_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_media_session_handler_class);

  g_type_class_add_private (gabble_media_session_handler_class, sizeof (GabbleMediaSessionHandlerPrivate));

  object_class->dispose = gabble_media_session_handler_dispose;
  object_class->finalize = gabble_media_session_handler_finalize;

  signals[NEW_MEDIA_STREAM_HANDLER] =
    g_signal_new ("new-media-stream-handler",
                  G_OBJECT_CLASS_TYPE (gabble_media_session_handler_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_session_handler_marshal_VOID__STRING_INT_INT,
                  G_TYPE_NONE, 3, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_UINT, G_TYPE_UINT);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_media_session_handler_class), &dbus_glib_gabble_media_session_handler_object_info);
}

void
gabble_media_session_handler_dispose (GObject *object)
{
  GabbleMediaSessionHandler *self = GABBLE_MEDIA_SESSION_HANDLER (object);
  GabbleMediaSessionHandlerPrivate *priv = GABBLE_MEDIA_SESSION_HANDLER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_media_session_handler_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_media_session_handler_parent_class)->dispose (object);
}

void
gabble_media_session_handler_finalize (GObject *object)
{
  GabbleMediaSessionHandler *self = GABBLE_MEDIA_SESSION_HANDLER (object);
  GabbleMediaSessionHandlerPrivate *priv = GABBLE_MEDIA_SESSION_HANDLER_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (gabble_media_session_handler_parent_class)->finalize (object);
}



/**
 * gabble_media_session_handler_error
 *
 * Implements DBus method Error
 * on interface org.freedesktop.Telepathy.Media.SessionHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_session_handler_error (GabbleMediaSessionHandler *obj, guint errno, const gchar * message, GError **error)
{
  return TRUE;
}


/**
 * gabble_media_session_handler_ready
 *
 * Implements DBus method Ready
 * on interface org.freedesktop.Telepathy.Media.SessionHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_session_handler_ready (GabbleMediaSessionHandler *obj, GError **error)
{
  return TRUE;
}

