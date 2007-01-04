#ifndef __GABBLE_TELEPATHY_ERRORS_H__
#define __GABBLE_TELEPATHY_ERRORS_H__

#include <telepathy-glib/tp-errors.h>
#define NetworkError TpError_NetworkError
#define NotImplemented TpError_NotImplemented
#define InvalidArgument TpError_InvalidArgument
#define NotAvailable TpError_NotAvailable
#define Disconnected TpError_Disconnected
#define PermissionDenied TpError_PermissionDenied
#define InvalidHandle TpError_InvalidHandle
#define ChannelBanned TpError_ChannelBanned
#define ChannelFull TpError_ChannelFull
#define ChannelInviteOnly TpError_ChannelInviteOnly

#endif
