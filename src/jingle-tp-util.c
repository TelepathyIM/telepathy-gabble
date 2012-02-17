/*
 * jingle-tp-util.c - Telepathy-flavoured Jingle utility functions
 * Copyright © 2008–2012 Collabora Ltd.
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

#include "jingle-tp-util.h"

JingleMediaType
jingle_media_type_from_tp (TpMediaStreamType type)
{
  switch (type)
    {
      case TP_MEDIA_STREAM_TYPE_AUDIO:
        return JINGLE_MEDIA_TYPE_AUDIO;
      case TP_MEDIA_STREAM_TYPE_VIDEO:
        return JINGLE_MEDIA_TYPE_VIDEO;
      default:
        g_return_val_if_reached (JINGLE_MEDIA_TYPE_NONE);
    }
}

TpMediaStreamType
jingle_media_type_to_tp (JingleMediaType type)
{
  switch (type)
    {
      case JINGLE_MEDIA_TYPE_AUDIO:
        return TP_MEDIA_STREAM_TYPE_AUDIO;
      case JINGLE_MEDIA_TYPE_VIDEO:
        return TP_MEDIA_STREAM_TYPE_VIDEO;
      default:
        g_return_val_if_reached (TP_MEDIA_STREAM_TYPE_AUDIO);
    }
}
