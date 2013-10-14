/* XML console plugin
 *
 * Copyright © 2011 Collabora Ltd. <http://www.collabora.co.uk/>
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

#include <glib-object.h>
#include <telepathy-glib/telepathy-glib.h>

typedef struct _GabbleConsoleChannel GabbleConsoleChannel;
typedef struct _GabbleConsoleChannelClass GabbleConsoleChannelClass;
typedef struct _GabbleConsoleChannelPrivate GabbleConsoleChannelPrivate;

struct _GabbleConsoleChannel {
    TpBaseChannel parent;

    GabbleConsoleChannelPrivate *priv;
};

struct _GabbleConsoleChannelClass {
    TpBaseChannelClass parent;
};

GType gabble_console_channel_get_type (void);

#define GABBLE_TYPE_CONSOLE_CHANNEL \
  (gabble_console_channel_get_type ())
#define GABBLE_CONSOLE_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GABBLE_TYPE_CONSOLE_CHANNEL, \
                               GabbleConsoleChannel))
#define GABBLE_CONSOLE_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GABBLE_TYPE_CONSOLE_CHANNEL, \
                            GabbleConsoleChannelClass))
#define GABBLE_IS_CONSOLE_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GABBLE_TYPE_CONSOLE_CHANNEL))
#define GABBLE_IS_CONSOLE_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GABBLE_TYPE_CONSOLE_CHANNEL))
#define GABBLE_CONSOLE_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_CONSOLE_CHANNEL, \
                              GabbleConsoleChannelClass))
