/*
 * telepathy-errors.c - Source for D-Bus error types used in telepathy
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

#include <telepathy-glib/errors.h>

#include <glib.h>

/**
 * tp_g_set_error_invalid_handle_type:
 * @type: An invalid handle type
 * @error: Either %NULL, or used to return an error (as for g_set_error)
 *
 * Set the error InvalidArgument corresponding to an invalid handle type,
 * with an appropriate message.
 */
void
tp_g_set_error_invalid_handle_type (guint type, GError **error)
{
  g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
      "invalid handle type %u", type);
}

/**
 * tp_g_set_error_unsupported_handle_type:
 * @type: An unsupported handle type
 * @error: Either %NULL, or used to return an error (as for g_set_error)
 *
 * Set the error InvalidArgument for a handle type which is valid but is not
 * supported by this connection manager, with an appropriate message.
 *
 * FIXME: Shouldn't the error be NotImplemented? The spec doesn't always
 * allow us to return that, though.
 */
void
tp_g_set_error_unsupported_handle_type (guint type, GError **error)
{
  g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
      "unsupported handle type %u", type);
}

/**
 * tp_errors_quark:
 *
 * <!--no need for more documentation, Returns: says it all-->
 *
 * Returns: the Telepathy error domain.
 */
GQuark
tp_errors_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("tp_errors");
  return quark;
}
