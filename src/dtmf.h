/* DTMF utility functions
 *
 * Copyright © 2010 Collabora Ltd.
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

#ifndef GABBLE_DTMF_H
#define GABBLE_DTMF_H

#include <glib-object.h>
#include <telepathy-glib/enums.h>

gchar gabble_dtmf_event_to_char (TpDTMFEvent event);

typedef struct _GabbleDTMFPlayer GabbleDTMFPlayer;
typedef struct _GabbleDTMFPlayerClass GabbleDTMFPlayerClass;
typedef struct _GabbleDTMFPlayerPrivate GabbleDTMFPlayerPrivate;

GType gabble_dtmf_player_get_type (void);

#define GABBLE_TYPE_DTMF_PLAYER \
  (gabble_dtmf_player_get_type ())
#define GABBLE_DTMF_PLAYER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GABBLE_TYPE_DTMF_PLAYER, \
                               GabbleDTMFPlayer))
#define GABBLE_DTMF_PLAYER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GABBLE_TYPE_DTMF_PLAYER, \
                            GabbleDTMFPlayerClass))
#define GABBLE_IS_DTMF_PLAYER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GABBLE_TYPE_DTMF_PLAYER))
#define GABBLE_IS_DTMF_PLAYER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GABBLE_TYPE_DTMF_PLAYER))
#define GABBLE_DTMF_PLAYER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_DTMF_PLAYER, \
                              GabbleDTMFPlayerClass))

struct _GabbleDTMFPlayer
{
  GObject parent;
  GabbleDTMFPlayerPrivate *priv;
};

struct _GabbleDTMFPlayerClass
{
  GObjectClass parent_class;
  gpointer priv;
};

GabbleDTMFPlayer *gabble_dtmf_player_new (void);

gboolean gabble_dtmf_player_play (GabbleDTMFPlayer *self,
    const gchar *tones, guint tone_ms, guint gap_ms, guint pause_ms,
    GError **error);

gboolean gabble_dtmf_player_is_active (GabbleDTMFPlayer *self);

void gabble_dtmf_player_cancel (GabbleDTMFPlayer *self);

#endif
