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

#ifndef WOCKY_JINGLE_INFO_H
#define WOCKY_JINGLE_INFO_H

#include <glib-object.h>
#include <wocky/wocky.h>

typedef struct _WockyJingleInfo WockyJingleInfo;
typedef struct _WockyJingleInfoClass WockyJingleInfoClass;
typedef struct _WockyJingleInfoPrivate WockyJingleInfoPrivate;

struct _WockyJingleInfoClass {
    GObjectClass parent_class;
};

struct _WockyJingleInfo {
    GObject parent;

    WockyJingleInfoPrivate *priv;
};

GType wocky_jingle_info_get_type (void);

WockyJingleInfo *wocky_jingle_info_new (
    WockyPorter *porter);

void wocky_jingle_info_take_stun_server (
    WockyJingleInfo *self,
    gchar *stun_server,
    guint16 stun_port,
    gboolean is_fallback);
void wocky_jingle_info_send_request (
    WockyJingleInfo *self,
    gboolean google_jingleinfo_supported);

typedef struct {
    gchar *address;
    guint16 port;
} WockyStunServer;

GList *wocky_jingle_info_get_stun_servers (
    WockyJingleInfo *self);

const gchar *wocky_jingle_info_get_google_relay_token (
    WockyJingleInfo *self);

typedef enum {
    WOCKY_JINGLE_RELAY_TYPE_UDP,
    WOCKY_JINGLE_RELAY_TYPE_TCP,
    WOCKY_JINGLE_RELAY_TYPE_TLS
} WockyJingleRelayType;
#define WOCKY_N_JINGLE_RELAY_TYPES 3

typedef struct {
    WockyJingleRelayType type;
    gchar *ip;
    guint port;
    gchar *username;
    gchar *password;
    guint component;
} WockyJingleRelay;

WockyJingleRelay *wocky_jingle_relay_new (
    WockyJingleRelayType type,
    const gchar *ip,
    guint port,
    const gchar *username,
    const gchar *password,
    guint component);
void wocky_jingle_relay_free (WockyJingleRelay *relay);

/*
 * @relays: (element-type WockyJingleRelay) (transfer none): a possibly-empty
 *  array of WockyJingleRelay structs.
 */
typedef void (*WockyJingleInfoRelaySessionCb) (
    GPtrArray *relays,
    gpointer user_data);
void wocky_jingle_info_create_google_relay_session (
    WockyJingleInfo *self,
    guint components,
    WockyJingleInfoRelaySessionCb callback,
    gpointer user_data);

void wocky_jingle_info_set_test_mode (void);

/* TYPE MACROS */
#define WOCKY_TYPE_JINGLE_INFO \
  (wocky_jingle_info_get_type ())
#define WOCKY_JINGLE_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), WOCKY_TYPE_JINGLE_INFO, WockyJingleInfo))
#define WOCKY_JINGLE_INFO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), WOCKY_TYPE_JINGLE_INFO,\
                           WockyJingleInfoClass))
#define WOCKY_IS_JINGLE_INFO(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), WOCKY_TYPE_JINGLE_INFO))
#define WOCKY_IS_JINGLE_INFO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), WOCKY_TYPE_JINGLE_INFO))
#define WOCKY_JINGLE_INFO_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), WOCKY_TYPE_JINGLE_INFO, \
                              WockyJingleInfoClass))

#endif /* WOCKY_JINGLE_INFO_H */
