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

#include "config.h"
#include "dtmf.h"

#include <telepathy-glib/errors.h>
#include <telepathy-glib/util.h>

/**
 * gabble_dtmf_event_to_char:
 * @event: a TpDTMFEvent
 *
 * Return a printable ASCII character representing @event, or '?' if @event
 * was not understood.
 */
gchar
gabble_dtmf_event_to_char (TpDTMFEvent event)
{
  switch (event)
    {
      case TP_DTMF_EVENT_DIGIT_0:
      case TP_DTMF_EVENT_DIGIT_1:
      case TP_DTMF_EVENT_DIGIT_2:
      case TP_DTMF_EVENT_DIGIT_3:
      case TP_DTMF_EVENT_DIGIT_4:
      case TP_DTMF_EVENT_DIGIT_5:
      case TP_DTMF_EVENT_DIGIT_6:
      case TP_DTMF_EVENT_DIGIT_7:
      case TP_DTMF_EVENT_DIGIT_8:
      case TP_DTMF_EVENT_DIGIT_9:
        return '0' + (event - TP_DTMF_EVENT_DIGIT_0);

      case TP_DTMF_EVENT_ASTERISK:
        return '*';

      case TP_DTMF_EVENT_HASH:
        return '#';

      case TP_DTMF_EVENT_LETTER_A:
      case TP_DTMF_EVENT_LETTER_B:
      case TP_DTMF_EVENT_LETTER_C:
      case TP_DTMF_EVENT_LETTER_D:
        return 'A' + (event - TP_DTMF_EVENT_LETTER_A);

      default:
        return '?';
    }
}

#define INVALID_DTMF_EVENT ((TpDTMFEvent) 0xFF)

static TpDTMFEvent
gabble_dtmf_char_to_event (gchar c)
{
  switch (c)
    {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        return TP_DTMF_EVENT_DIGIT_0 + (c - '0');

      case 'A':
      case 'B':
      case 'C':
      case 'D':
        return TP_DTMF_EVENT_LETTER_A + (c - 'A');

      /* not strictly valid but let's be nice to people */
      case 'a':
      case 'b':
      case 'c':
      case 'd':
        return TP_DTMF_EVENT_LETTER_A + (c - 'a');

      case '*':
        return TP_DTMF_EVENT_ASTERISK;

      case '#':
        return TP_DTMF_EVENT_HASH;

      default:
        return INVALID_DTMF_EVENT;
    }
}

G_DEFINE_TYPE (GabbleDTMFPlayer, gabble_dtmf_player, G_TYPE_OBJECT)

struct _GabbleDTMFPlayerPrivate
{
  /* owned, or NULL */
  gchar *dialstring;
  /* a pointer into dialstring, or NULL */
  const gchar *dialstring_remaining;
  guint timer_id;
  guint tone_ms;
  guint gap_ms;
  gboolean playing_tone;
};

static guint sig_id_started_tone;
static guint sig_id_stopped_tone;
static guint sig_id_finished;

static void
gabble_dtmf_player_emit_started_tone (GabbleDTMFPlayer *self,
    TpDTMFEvent tone)
{
  self->priv->playing_tone = TRUE;
  g_signal_emit (self, sig_id_started_tone, 0, tone);
}

static void
gabble_dtmf_player_maybe_emit_stopped_tone (GabbleDTMFPlayer *self)
{
  if (!self->priv->playing_tone)
    return;

  self->priv->playing_tone = FALSE;
  g_signal_emit (self, sig_id_stopped_tone, 0);
}

static void
gabble_dtmf_player_emit_finished (GabbleDTMFPlayer *self,
    gboolean cancelled)
{
  g_signal_emit (self, sig_id_finished, 0, cancelled);
}

void
gabble_dtmf_player_cancel (GabbleDTMFPlayer *self)
{
  if (self->priv->timer_id != 0)
    {
      gabble_dtmf_player_maybe_emit_stopped_tone (self);
      gabble_dtmf_player_emit_finished (self, TRUE);

      g_source_remove (self->priv->timer_id);
      self->priv->timer_id = 0;
    }

  tp_clear_pointer (&self->priv->dialstring, g_free);
}

static gboolean
gabble_dtmf_player_timer_cb (gpointer data)
{
  GabbleDTMFPlayer *self = data;
  gboolean was_playing = self->priv->playing_tone;

  self->priv->timer_id = 0;

  gabble_dtmf_player_maybe_emit_stopped_tone (self);

  if (tp_str_empty (self->priv->dialstring_remaining))
    {
      /* die of natural causes */
      gabble_dtmf_player_emit_finished (self, FALSE);
      gabble_dtmf_player_cancel (self);
      return FALSE;
    }

  if (was_playing)
    {
      /* We're at the end of a tone. Advance to the next tone, but before
       * playing it, play some silence. */
      self->priv->dialstring_remaining++;
      self->priv->timer_id = g_timeout_add (self->priv->gap_ms,
          gabble_dtmf_player_timer_cb, self);
    }
  else
    {
      /* Either we're in our initial state or we're at the end of a gap.
       * Play the next tone now. */
      gabble_dtmf_player_emit_started_tone (self,
          gabble_dtmf_char_to_event (*self->priv->dialstring_remaining));
      self->priv->timer_id = g_timeout_add (self->priv->tone_ms,
          gabble_dtmf_player_timer_cb, self);
    }

  return FALSE;
}

gboolean
gabble_dtmf_player_play (GabbleDTMFPlayer *self,
    const gchar *tones,
    guint tone_ms,
    guint gap_ms,
    GError **error)
{
  guint i;

  g_return_val_if_fail (tones != NULL, FALSE);
  g_return_val_if_fail (tone_ms > 0, FALSE);
  g_return_val_if_fail (gap_ms > 0, FALSE);

  if (self->priv->dialstring != NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_SERVICE_BUSY,
          "DTMF tones are already being played");
      return FALSE;
    }

  g_assert (self->priv->timer_id == 0);

  for (i = 0; tones[i] != '\0'; i++)
    {
      if (gabble_dtmf_char_to_event (tones[i]) == INVALID_DTMF_EVENT)
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "Invalid character in DTMF string starting at %s",
              tones + i);
          return FALSE;
        }
    }

  self->priv->dialstring = g_strdup (tones);
  self->priv->dialstring_remaining = self->priv->dialstring;
  self->priv->tone_ms = tone_ms;
  self->priv->gap_ms = gap_ms;

  /* start off the process: conceptually, this is the end of the zero-length
   * gap before the first tone */
  self->priv->playing_tone = FALSE;
  gabble_dtmf_player_timer_cb (self);
  return TRUE;
}

gboolean
gabble_dtmf_player_is_active (GabbleDTMFPlayer *self)
{
  return (self->priv->dialstring != NULL);
}

#define MY_PARENT_CLASS (gabble_dtmf_player_parent_class)

static void
gabble_dtmf_player_init (GabbleDTMFPlayer *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GABBLE_TYPE_DTMF_PLAYER,
      GabbleDTMFPlayerPrivate);

  self->priv->dialstring = NULL;
  self->priv->dialstring_remaining = NULL;
  self->priv->playing_tone = FALSE;
  self->priv->timer_id = 0;
}

static void
gabble_dtmf_player_dispose (GObject *object)
{
  GabbleDTMFPlayer *self = (GabbleDTMFPlayer *) object;
  void (*dispose) (GObject *) = G_OBJECT_CLASS (MY_PARENT_CLASS)->dispose;

  gabble_dtmf_player_cancel (self);

  if (dispose != NULL)
    dispose (object);
}

static void
gabble_dtmf_player_class_init (GabbleDTMFPlayerClass *cls)
{
  GObjectClass *object_class = (GObjectClass *) cls;

  g_type_class_add_private (cls, sizeof (GabbleDTMFPlayerPrivate));

  object_class->dispose = gabble_dtmf_player_dispose;

  sig_id_started_tone =  g_signal_new ("started-tone",
      G_OBJECT_CLASS_TYPE (cls), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);

  sig_id_stopped_tone =  g_signal_new ("stopped-tone",
      G_OBJECT_CLASS_TYPE (cls), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  sig_id_finished =  g_signal_new ("finished",
      G_OBJECT_CLASS_TYPE (cls), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

GabbleDTMFPlayer *
gabble_dtmf_player_new (void)
{
  return g_object_new (GABBLE_TYPE_DTMF_PLAYER,
      NULL);
}
