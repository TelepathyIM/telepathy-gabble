/* Generated from the Telepathy spec

Copyright (C) 2005, 2006, 2007 Collabora Limited
Copyright (C) 2005, 2006, 2007 Nokia Corporation


This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
  
*/

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {

    /* org.freedesktop.Telepathy.Error.NetworkError
    Raised when there is an error reading from or writing to the network.
     */
    TP_ERROR_NETWORK_ERROR,

    /* org.freedesktop.Telepathy.Error.NotImplemented
    Raised when the requested method, channel, etc is not available on this connection.
     */
    TP_ERROR_NOT_IMPLEMENTED,

    /* org.freedesktop.Telepathy.Error.InvalidArgument
    Raised when one of the provided arguments is invalid.
     */
    TP_ERROR_INVALID_ARGUMENT,

    /* org.freedesktop.Telepathy.Error.NotAvailable
    Raised when the requested functionality is temporarily unavailable.
     */
    TP_ERROR_NOT_AVAILABLE,

    /* org.freedesktop.Telepathy.Error.PermissionDenied
    The user is not permitted to perform the requested operation.
     */
    TP_ERROR_PERMISSION_DENIED,

    /* org.freedesktop.Telepathy.Error.Disconnected
    The connection is not currently connected and cannot be used.
     */
    TP_ERROR_DISCONNECTED,

    /* org.freedesktop.Telepathy.Error.InvalidHandle
    The contact name specified is unknown on this channel or connection.
     */
    TP_ERROR_INVALID_HANDLE,

    /* org.freedesktop.Telepathy.Error.Channel.Banned
    You are banned from the channel.
     */
    TP_ERROR_CHANNEL_BANNED,

    /* org.freedesktop.Telepathy.Error.Channel.Full
    The channel is full.
     */
    TP_ERROR_CHANNEL_FULL,

    /* org.freedesktop.Telepathy.Error.Channel.InviteOnly
    The requested channel is invite-only.
     */
    TP_ERROR_CHANNEL_INVITE_ONLY,
} TpError;

#ifdef __cplusplus
}
#endif
