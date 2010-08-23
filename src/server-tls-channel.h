/*
 * server-tls-channel.h - Header for GabbleServerTLSChannel
 * Copyright (C) 2010 Collabora Ltd.
 * @author Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
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

#ifndef __GABBLE_SERVER_TLS_CHANNEL_H__
#define __GABBLE_SERVER_TLS_CHANNEL_H__

#include <glib-object.h>

#include <telepathy-glib/base-connection.h>

#include <extensions/extensions.h>

#include "base-channel.h"
#include "tls-certificate.h"

G_BEGIN_DECLS

typedef struct _GabbleServerTLSChannelPrivate GabbleServerTLSChannelPrivate;
typedef struct _GabbleServerTLSChannelClass GabbleServerTLSChannelClass;
typedef struct _GabbleServerTLSChannel GabbleServerTLSChannel;

struct _GabbleServerTLSChannelClass {
  GabbleBaseChannelClass base_class;
};

struct _GabbleServerTLSChannel {
  GabbleBaseChannel parent;

  GabbleServerTLSChannelPrivate *priv;
};

GType gabble_server_tls_channel_get_type (void);

#define GABBLE_TYPE_SERVER_TLS_CHANNEL \
  (gabble_server_tls_channel_get_type ())
#define GABBLE_SERVER_TLS_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_SERVER_TLS_CHANNEL, \
      GabbleServerTLSChannel))
#define GABBLE_SERVER_TLS_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_SERVER_TLS_CHANNEL, \
      GabbleServerTLSChannelClass))
#define GABBLE_IS_SERVER_TLS_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_SERVER_TLS_CHANNEL))
#define GABBLE_IS_SERVER_TLS_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_SERVER_TLS_CHANNEL))
#define GABBLE_SERVER_TLS_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_SERVER_TLS_CHANNEL,\
      GabbleServerTLSChannelClass))

void gabble_server_tls_channel_close (GabbleServerTLSChannel *self);

GabbleTLSCertificate * gabble_server_tls_channel_get_certificate (
    GabbleServerTLSChannel *self);

G_END_DECLS

#endif /* #ifndef __GABBLE_SERVER_TLS_CHANNEL_H__*/
