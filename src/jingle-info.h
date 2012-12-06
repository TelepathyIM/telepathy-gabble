/*
 * jingle-info.h - exciting times with Google's jingleinfo extension
 * Copyright © 2008–2012 Collabora Ltd.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef GABBLE_JINGLE_INFO_H
#define GABBLE_JINGLE_INFO_H

#include <glib-object.h>
#include <wocky/wocky.h>

typedef struct _GabbleJingleInfo GabbleJingleInfo;
typedef struct _GabbleJingleInfoClass GabbleJingleInfoClass;
typedef struct _GabbleJingleInfoPrivate GabbleJingleInfoPrivate;

struct _GabbleJingleInfoClass {
    GObjectClass parent_class;
};

struct _GabbleJingleInfo {
    GObject parent;

    GabbleJingleInfoPrivate *priv;
};

GType gabble_jingle_info_get_type (void);

GabbleJingleInfo *gabble_jingle_info_new (
    WockyPorter *porter);

void gabble_jingle_info_take_stun_server (
    GabbleJingleInfo *self,
    gchar *stun_server,
    guint16 stun_port,
    gboolean is_fallback);
void gabble_jingle_info_send_request (
    GabbleJingleInfo *self,
    gboolean google_jingleinfo_supported);

typedef struct {
    gchar *address;
    guint16 port;
} GabbleStunServer;

GList *gabble_jingle_info_get_stun_servers (
    GabbleJingleInfo *self);

const gchar *gabble_jingle_info_get_google_relay_token (
    GabbleJingleInfo *self);

typedef enum {
    GABBLE_JINGLE_RELAY_TYPE_UDP,
    GABBLE_JINGLE_RELAY_TYPE_TCP,
    GABBLE_JINGLE_RELAY_TYPE_TLS
} GabbleJingleRelayType;
#define GABBLE_N_JINGLE_RELAY_TYPES 3

typedef struct {
    GabbleJingleRelayType type;
    gchar *ip;
    guint port;
    gchar *username;
    gchar *password;
    guint component;
} GabbleJingleRelay;

GabbleJingleRelay *gabble_jingle_relay_new (
    GabbleJingleRelayType type,
    const gchar *ip,
    guint port,
    const gchar *username,
    const gchar *password,
    guint component);
void gabble_jingle_relay_free (GabbleJingleRelay *relay);

/*
 * @relays: (element-type GabbleJingleRelay) (transfer none): a possibly-empty
 *  array of GabbleJingleRelay structs.
 */
typedef void (*GabbleJingleInfoRelaySessionCb) (
    GPtrArray *relays,
    gpointer user_data);
void gabble_jingle_info_create_google_relay_session (
    GabbleJingleInfo *self,
    guint components,
    GabbleJingleInfoRelaySessionCb callback,
    gpointer user_data);

void gabble_jingle_info_set_test_mode (void);

/* TYPE MACROS */
#define GABBLE_TYPE_JINGLE_INFO \
  (gabble_jingle_info_get_type ())
#define GABBLE_JINGLE_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_JINGLE_INFO, GabbleJingleInfo))
#define GABBLE_JINGLE_INFO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_JINGLE_INFO,\
                           GabbleJingleInfoClass))
#define GABBLE_IS_JINGLE_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_JINGLE_INFO))
#define GABBLE_IS_JINGLE_INFO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_JINGLE_INFO))
#define GABBLE_JINGLE_INFO_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_JINGLE_INFO, \
                              GabbleJingleInfoClass))

#endif /* GABBLE_JINGLE_INFO_H */
