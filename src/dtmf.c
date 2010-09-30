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
