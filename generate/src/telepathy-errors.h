/*
 * telepathy-errors.h - Header for Telepathy error types
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

#ifndef __TELEPATHY_ERRORS_H__
#define __TELEPATHY_ERRORS_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum
{
  InvalidHandle,    /** The contact name specified is unknown on this channel
                     *  or connection.
                     */
  Disconnected,     /** The connection is not currently connected and cannot be
                     *  used.
                     */
  InvalidArgument,  /** Raised when one of the provided arguments is invalid.
                     */
  NetworkError,     /** Raised when there is an error reading from or writing
                     *  to the network.
                     */
  PermissionDenied, /** The user is not permitted to perform the requested
                     *  operation.
                     */
  NotAvailable,     /** Raised when the requested functionality is temporarily
                     *  unavailable.
                     */
  NotImplemented,   /** Raised when the requested method, channel, etc is not
                     *  available on this connection.
                     */
} TelepathyErrors; 


G_END_DECLS

#endif /* #ifndef __TELEPATHY_ERRORS_H__*/
