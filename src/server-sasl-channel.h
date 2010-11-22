/*
 * server-sasl-channel.h - Header for GabbleServerSaslChannel
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

#ifndef __GABBLE_SERVER_SASL_CHANNEL_H__
#define __GABBLE_SERVER_SASL_CHANNEL_H__

#include <glib-object.h>

#include <telepathy-glib/base-connection.h>
#include <wocky/wocky-auth-registry.h>

#include <extensions/extensions.h>

#include "types.h"

G_BEGIN_DECLS

typedef struct _GabbleServerSaslChannelPrivate GabbleServerSaslChannelPrivate;
typedef struct _GabbleServerSaslChannelClass GabbleServerSaslChannelClass;
typedef struct _GabbleServerSaslChannel GabbleServerSaslChannel;

struct _GabbleServerSaslChannelClass {
    WockyAuthRegistryClass parent_class;

    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _GabbleServerSaslChannel {
    WockyAuthRegistry parent;

    GabbleServerSaslChannelPrivate *priv;
};

GType gabble_server_sasl_channel_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_SERVER_SASL_CHANNEL \
  (gabble_server_sasl_channel_get_type ())
#define GABBLE_SERVER_SASL_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_SERVER_SASL_CHANNEL,\
                              GabbleServerSaslChannel))
#define GABBLE_SERVER_SASL_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_SERVER_SASL_CHANNEL,\
                           GabbleServerSaslChannelClass))
#define GABBLE_IS_SERVER_SASL_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_SERVER_SASL_CHANNEL))
#define GABBLE_IS_SERVER_SASL_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_SERVER_SASL_CHANNEL))
#define GABBLE_SERVER_SASL_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_SERVER_SASL_CHANNEL,\
                              GabbleServerSaslChannelClass))

void gabble_server_sasl_channel_close (GabbleServerSaslChannel *self);

GabbleServerSaslChannel *gabble_server_sasl_channel_new (
    GabbleConnection *conn, GStrv available_mechanisms,
    gboolean secure, const gchar *session_id);

void gabble_server_sasl_channel_start_auth_async (
    GabbleServerSaslChannel *self, GAsyncReadyCallback callback,
    gpointer user_data);
gboolean gabble_server_sasl_channel_start_auth_finish (
    GabbleServerSaslChannel *self, GAsyncResult *result,
    WockyAuthRegistryStartData **start_data, GError **error);

void gabble_server_sasl_channel_challenge_async (GabbleServerSaslChannel *self,
    const GString *challenge_data, GAsyncReadyCallback callback,
    gpointer user_data);
gboolean gabble_server_sasl_channel_challenge_finish (
    GabbleServerSaslChannel *self, GAsyncResult *result, GString **response,
    GError **error);

void gabble_server_sasl_channel_success_async (GabbleServerSaslChannel *self,
    GAsyncReadyCallback callback, gpointer user_data);
gboolean gabble_server_sasl_channel_success_finish (
    GabbleServerSaslChannel *self, GAsyncResult *result, GError **error);

gboolean gabble_server_sasl_channel_is_open (GabbleServerSaslChannel *self);

G_END_DECLS

#endif /* #ifndef __GABBLE_SERVER_SASL_CHANNEL_H__*/
