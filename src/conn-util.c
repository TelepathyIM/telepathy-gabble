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

#include <gabble/gabble.h>

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
    {
      g_simple_async_result_set_op_res_gpointer (result, reply,
          (GDestroyNotify) g_object_unref);
    }
  else
    {
      g_simple_async_result_set_from_error (result, error);
      g_clear_error (&error);
    }

  g_simple_async_result_complete (result);
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

gboolean
conn_util_send_iq_finish (GabbleConnection *self,
    GAsyncResult *result,
    WockyStanza **response,
    GError **error)
{
  GSimpleAsyncResult *res;
  WockyStanza *resp;
  GError *err = NULL;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
          G_OBJECT (self), conn_util_send_iq_async), FALSE);

  res = (GSimpleAsyncResult *) result;

  resp = g_simple_async_result_get_op_res_gpointer (res);

  if (g_simple_async_result_propagate_error (res, &err) ||
      wocky_stanza_extract_errors (resp, NULL, &err, NULL, NULL))
    {
      gabble_set_tp_error_from_wocky (err, error);
      g_error_free (err);

      return FALSE;
    }

  if (response != NULL)
    *response = g_object_ref (resp);

  return TRUE;
}

const gchar *
conn_util_get_bare_self_jid (GabbleConnection *conn)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (conn);
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  TpHandle self = tp_base_connection_get_self_handle (base);

  return tp_handle_inspect (contact_handles, self);
}
