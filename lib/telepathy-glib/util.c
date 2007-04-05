/*
 * util.c - Source for telepathy-glib utility functions
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
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

/**
 * SECTION:util
 * @title: Utilities
 * @short_description: Non-Telepathy utility functions
 *
 * Some utility functions used in telepathy-glib which could have been in
 * GLib, but aren't.
 */

#include <telepathy-glib/util.h>

#include <string.h>


/**
 * tp_g_ptr_array_contains:
 * @haystack: The pointer array to be searched
 * @needle: The pointer to look for
 *
 * <!--no further documentation needed-->
 *
 * Returns: %TRUE if @needle is one of the elements of @haystack
 */

gboolean
tp_g_ptr_array_contains (GPtrArray *haystack, gpointer needle)
{
  guint i;

  for (i = 0; i < haystack->len; i++)
    {
      if (g_ptr_array_index (haystack, i) == needle)
        return TRUE;
    }

  return FALSE;
}


/**
 * tp_g_value_slice_free:
 * @value: A GValue which was allocated with the g_slice API
 *
 * Unset and free a slice-allocated GValue.
 */

void
tp_g_value_slice_free (GValue *value)
{
  g_value_unset (value);
  g_slice_free (GValue, value);
}


/**
 * tp_strdiff:
 * @left: The first string to compare (may be NULL)
 * @right: The second string to compare (may be NULL)
 *
 * Return %TRUE if the given strings are different. Unlike #strcmp this
 * function will handle null pointers, treating them as distinct from any
 * string.
 *
 * Returns: %FALSE if @left and @right are both %NULL, or if
 *          neither is %NULL and both have the same contents; %TRUE otherwise
 */

gboolean
tp_strdiff (const gchar *left, const gchar *right)
{
  if ((NULL == left) != (NULL == right))
    return TRUE;

  else if (left == right)
    return FALSE;

  else
    return (0 != strcmp (left, right));
}



/**
 * tp_mixin_offset_cast:
 * @instance: A pointer to a structure
 * @offset: The offset of a structure member in bytes, which must not be 0
 *
 * Extend a pointer by an offset, provided the offset is not 0.
 * This is used to cast from an object instance to one of the telepathy-glib
 * mixin classes.
 *
 * Returns: a pointer @offset bytes beyond @instance
 */
gpointer
tp_mixin_offset_cast (gpointer instance, guint offset)
{
  g_return_val_if_fail (offset != 0, NULL);

  return ((guchar *) instance + offset);
}

static inline gboolean
_esc_ident_bad (gchar c, gboolean is_first)
{
  return ((c < 'a' || c > 'z') &&
          (c < 'A' || c > 'Z') &&
          (c < '0' || c > '9' || is_first));
}


/**
 * tp_escape_as_identifier:
 * @name: The string to be escaped
 *
 * Escape an arbitrary string so it follows the rules for a C identifier,
 * and hence an object path component, interface element component,
 * bus name component or member name in D-Bus.
 *
 * Unlike g_strcanon this is a reversible encoding, so it preserves
 * distinctness.
 *
 * The escaping consists of replacing all non-alphanumerics, and the first
 * character if it's a digit, with an underscore and two lower-case hex
 * digits:
 *
 *    "0123abc_xyz\x01\xff" -> _30123abc_5fxyz_01_ff
 *
 * i.e. similar to URI encoding, but with _ taking the role of %, and a
 * smaller allowed set.
 *
 * Returns: the escaped string, which must be freed by the caller with #g_free
 */
gchar *
tp_escape_as_identifier (const gchar *name)
{
  gboolean bad = FALSE;
  size_t len = 0;
  GString *op;
  const gchar *ptr, *first_ok;

  g_return_val_if_fail (name != NULL, NULL);

  for (ptr = name; *ptr; ptr++)
    {
      if (_esc_ident_bad (*ptr, ptr == name))
        {
          bad = TRUE;
          len += 3;
        }
      else
        len++;
    }

  /* fast path if it's clean */
  if (!bad)
    return g_strdup (name);

  /* If strictly less than ptr, first_ok is the first uncopied safe character.
   */
  first_ok = name;
  op = g_string_sized_new (len);
  for (ptr = name; *ptr; ptr++)
    {
      if (_esc_ident_bad (*ptr, ptr == name))
        {
          /* copy preceding safe characters if any */
          if (first_ok < ptr)
            {
              g_string_append_len (op, first_ok, ptr - first_ok);
            }
          /* escape the unsafe character */
          g_string_append_printf (op, "_%02x", (unsigned char)(*ptr));
          /* restart after it */
          first_ok = ptr + 1;
        }
    }
  /* copy trailing safe characters if any */
  if (first_ok < ptr)
    {
      g_string_append_len (op, first_ok, ptr - first_ok);
    }
  return g_string_free (op, FALSE);
}
