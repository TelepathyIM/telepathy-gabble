/*
 * handles.c - mechanism to store and retrieve handles on a connection
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

#include <glib.h>
#include <string.h>

#include <telepathy-glib/heap.h>
#include <telepathy-glib/errors.h>

#include "handles.h"
#include "util.h"

#include "config.h"

/* public API */

/**
 * gabble_handle_jid_is_valid
 *
 * Validates a jid for given handle type and returns TRUE/FALSE
 * on success/failure. In the latter case further information is
 * provided through error if set.
 */
gboolean
gabble_handle_jid_is_valid (TpHandleType type, const gchar *jid, GError **error)
{
  if (type == TP_HANDLE_TYPE_CONTACT || type == TP_HANDLE_TYPE_ROOM)
    {
      if (!strchr (jid, '@'))
        {
          g_debug ("%s: jid %s has no @", G_STRFUNC, jid);

          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "jid %s has no @", jid);

          return FALSE;
        }

      /* FIXME: do more extensive checking */
    }
  else
    {
      g_assert_not_reached ();
      /* FIXME: add checking for other types here */
    }

  return TRUE;
}
