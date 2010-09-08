/*
 * conn-util.c - Header for Gabble connection kitchen-sink code.
 * Copyright (C) 2010 Collabora Ltd.
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

#include "conn-util.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION
#include "debug.h"
#include "namespaces.h"
#include "util.h"

#include <wocky/wocky-utils.h>

static void
conn_util_send_iq_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source_object);
  WockyStanza *reply;
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
  GError *error = NULL;

  reply = wocky_porter_send_iq_finish (porter, res, &error);

  if (reply != NULL)
    g_simple_async_result_set_op_res_gpointer (result, reply,
        (GDestroyNotify) g_object_unref);
  else
    g_simple_async_result_set_from_error (result, error);

  g_simple_async_result_complete_in_idle (result);

  g_object_unref (result);
}

void
conn_util_send_iq_async (GabbleConnection *self,
    WockyStanza *stanza,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  WockyPorter *porter = wocky_session_get_porter (self->session);
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, conn_util_send_iq_async);

  wocky_porter_send_iq_async (porter, stanza, cancellable,
      conn_util_send_iq_cb, result);
}

WockyStanza *
conn_util_send_iq_finish (GabbleConnection *self,
    GAsyncResult *result,
    GError **error)
{
  wocky_implement_finish_return_copy_pointer (self, conn_util_send_iq_async,
      g_object_ref);
}

/**
 * conn_util_send_iq_finish_harder:
 * @self: a #GabbleConnection
 * @result: the #GSimpleAsyncResult passed to a conn_util_send_iq_async()
 *  callback
 * @error: location at which to store a Telepathy D-Bus error, if any
 *
 * Throughout Gabble, we want to finish conn_util_send_iq_async() calls, and
 * transform an iq type='error' into a correspondingish Telepathy error, and
 * only process the reply further if it was a huge success. So this function
 * does that. It's only suitable for use if you don't care about non-XMPP Core
 * stanza errors. If you do, you should use conn_util_send_iq_finish() and
 * faff around with wocky_stanza_extract_errors() directly.
 *
 * Returns: an IQ of WOCKY_STANZA_SUB_TYPE_RESULT, or NULL if sending the IQ
 *          failed or the reply was WOCKY_STANZA_SUB_TYPE_ERROR.
 */
WockyStanza *
conn_util_send_iq_finish_harder (GabbleConnection *self,
    GAsyncResult *result,
    GError **error)
{
  WockyStanza *reply;
  GError *error_ = NULL;

  reply = conn_util_send_iq_finish (self, result, &error_);

  if (reply != NULL)
    {
      if (!wocky_stanza_extract_errors (reply, NULL, &error_, NULL, NULL))
        return reply;

      g_object_unref (reply);
    }

  gabble_set_tp_error_from_wocky (error_, error);
  g_clear_error (&error_);
  return NULL;
}
