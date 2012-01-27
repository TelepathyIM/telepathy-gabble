/*
 * lm-connection.c - Loudmouth-Wocky compatibility layer
 * Copyright (C) 2009 Collabora Ltd.
 * @author Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
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

#include "lm-connection.h"
#include "lm-message-handler.h"

void
lm_connection_unref (LmConnection *connection)
{
  g_cancellable_cancel (connection->iq_reply_cancellable);
  g_object_unref (connection->iq_reply_cancellable);
  connection->iq_reply_cancellable = NULL;

  if (connection->porter != NULL)
    {
      g_object_unref (connection->porter);
      connection->porter = NULL;
    }

  g_free (connection);
}

LmConnection *
lm_connection_new (void)
{
  LmConnection *connection;

  connection = g_malloc (sizeof (LmConnection));
  connection->porter = NULL;
  connection->iq_reply_cancellable = g_cancellable_new ();

  return connection;
}

void
lm_connection_set_porter (LmConnection *connection,
    WockyPorter *porter)
{
  g_assert (connection != NULL);
  g_assert (connection->porter == NULL);
  connection->porter = g_object_ref (porter);
}
