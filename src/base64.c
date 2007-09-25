/*
 * base64.c - Base 64 encoding/decoding implementation
 * Copyright (C) 2006 Collabora Ltd.
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

#include "base64.h"

#define DEBUG_FLAG GABBLE_DEBUG_VCARD
#include "debug.h"

#include <ctype.h>
#include <string.h>

#include <glib.h>

/*
|AAAA AABB|BBBB CCCC|CCDD DDDD|

0xFC = 1111 1100
0x03 = 0000 0011
0xF0 = 1111 0000
0x0F = 0000 1111
0xC0 = 1100 0000
0x3F = 0011 1111

3 input bytes = 4 output bytes;
2 input bytes = 2 output bytes;
1 input byte  = 1 output byte.
*/

static const gchar *encoding =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const guint decoding[256] =
{
  /* ... */
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0,
  /* + */
  62,
  /* ... */
   0, 0, 0,
  /* / , 0-9 */
  63,
  52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
  /* ... */
   0, 0, 0, 0, 0, 0, 0,
  /* A */
   0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12,
  13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
  /* ... */
   0, 0, 0, 0, 0, 0,
  /* a */
  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
  39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
};

#define GET_6_BITS_0(s) (((s)[0] & 0xFC) >> 2)
#define GET_6_BITS_1(s) (((s)[0] & 0x03) << 4) | \
                        (((s)[1] & 0xF0) >> 4)
#define GET_6_BITS_2(s) (((s)[1] & 0x0F) << 2) | \
                        (((s)[2] & 0xC0) >> 6)
#define GET_6_BITS_3(s) (((s)[2] & 0x3F) << 0)

#define GET_BYTE_0(s) (((decoding[(guchar)(s)[0]] & 0x3F) << 2) | \
                       ((decoding[(guchar)(s)[1]] & 0x30) >> 4))
#define GET_BYTE_1(s) (((decoding[(guchar)(s)[1]] & 0x0F) << 4) | \
                       ((decoding[(guchar)(s)[2]] & 0x3C) >> 2))
#define GET_BYTE_2(s) (((decoding[(guchar)(s)[2]] & 0x03) << 6) | \
                       ((decoding[(guchar)(s)[3]] & 0xFF) << 0))

gchar *base64_encode (guint len, const gchar *str, gboolean split_lines)
{
  guint i;
  GString *tmp;

  /* TODO: calculate requisite output string length and allocate that big a
   * GString */
  tmp = g_string_new ("");

  for (i = 0; i < len; i += 3)
    {
      guint c1, c2, c3, c4;

      if (split_lines && i > 0 && (i * 4) % 76 == 0)
          g_string_append_c (tmp, '\n');

      switch (i + 3 - len)
        {
        case 1:
          c1 = encoding[GET_6_BITS_0 (str + i)];
          c2 = encoding[GET_6_BITS_1 (str + i)];
          /* can't use GET_6_BITS_2 because str[i+2] is out of range */
          c3 = encoding[(str[i + 1] & 0x0f) << 2];
          c4 = '=';
          break;
        case 2:
          c1 = encoding[GET_6_BITS_0 (str + i)];
          /* can't use GET_6_BITS_1 because str[i+1] is out of range */
          c2 = encoding[(str[i] & 0x03) << 4];
          c3 = '=';
          c4 = '=';
          break;
        default:
          c1 = encoding[GET_6_BITS_0 (str + i)];
          c2 = encoding[GET_6_BITS_1 (str + i)];
          c3 = encoding[GET_6_BITS_2 (str + i)];
          c4 = encoding[GET_6_BITS_3 (str + i)];
        }

      g_string_append_printf (tmp, "%c%c%c%c", c1, c2, c3, c4);
    }

  return g_string_free (tmp, FALSE);
}

GString *base64_decode (const gchar *str)
{
  guint i;
  GString *tmp;
  char group[4];
  guint filled = 0;

  for (i = 0; str[i]; i++)
    {
      if (str[i] != 'A' &&
          str[i] != '=' &&
          !isspace (str[i]) &&
          decoding[(guchar) str[i]] == 0)
        {
          DEBUG ("bad character %x at byte %u", (guchar)str[i], i);
          return NULL;
        }
    }

  tmp = g_string_new ("");

  for (i = 0; str[i]; i++)
    {
      if (isspace (str[i]))
        continue;

      group[filled++] = str[i];

      if (filled == 4)
        {
          if (group[3] == '=')
            {
              if (group[2] == '=')
                {
                  g_string_append_c (tmp, GET_BYTE_0(group));
                }
              else
                {
                  g_string_append_c (tmp, GET_BYTE_0(group));
                  g_string_append_c (tmp, GET_BYTE_1(group));
                }
             }
           else
            {
              g_string_append_c (tmp, GET_BYTE_0(group));
              g_string_append_c (tmp, GET_BYTE_1(group));
              g_string_append_c (tmp, GET_BYTE_2(group));
            }
          filled = 0;
        }
    }

  if (filled)
    {
      DEBUG ("insufficient padding at end of base64 string:\n%s", str);
      g_string_free (tmp, TRUE);
      return NULL;
    }

  return tmp;
}


