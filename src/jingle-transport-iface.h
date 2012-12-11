/*
 * jingle-transport-iface.h - Header for WockyJingleTransportIface
 * Copyright (C) 2007-2008 Collabora Ltd.
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

#ifndef __WOCKY_JINGLE_TRANSPORT_IFACE_H__
#define __WOCKY_JINGLE_TRANSPORT_IFACE_H__

#include <glib-object.h>
#include <wocky/wocky.h>

#include "jingle-factory.h"
#include "jingle-types.h"

G_BEGIN_DECLS

typedef enum
{
  WOCKY_JINGLE_TRANSPORT_STATE_DISCONNECTED,
  WOCKY_JINGLE_TRANSPORT_STATE_CONNECTING,
  WOCKY_JINGLE_TRANSPORT_STATE_CONNECTED
} WockyJingleTransportState;

typedef struct _WockyJingleTransportIface WockyJingleTransportIface;
typedef struct _WockyJingleTransportIfaceClass WockyJingleTransportIfaceClass;

struct _WockyJingleTransportIfaceClass {
  GTypeInterface parent;

  void (*parse_candidates) (WockyJingleTransportIface *,
    WockyNode *, GError **);

  void (*new_local_candidates) (WockyJingleTransportIface *, GList *);
  void (*inject_candidates) (WockyJingleTransportIface *,
      WockyNode *transport_node);
  void (*send_candidates) (WockyJingleTransportIface *, gboolean all);
  gboolean (*can_accept) (WockyJingleTransportIface *);

  GList * (*get_remote_candidates) (WockyJingleTransportIface *);
  GList * (*get_local_candidates) (WockyJingleTransportIface *);
  gboolean (*get_credentials) (WockyJingleTransportIface *,
      gchar **ufrag, gchar **pwd);

  WockyJingleTransportType (*get_transport_type) (void);
};

GType wocky_jingle_transport_iface_get_type (void);

/* TYPE MACROS */
#define WOCKY_TYPE_JINGLE_TRANSPORT_IFACE \
  (wocky_jingle_transport_iface_get_type ())
#define WOCKY_JINGLE_TRANSPORT_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), WOCKY_TYPE_JINGLE_TRANSPORT_IFACE, WockyJingleTransportIface))
#define WOCKY_IS_JINGLE_TRANSPORT_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), WOCKY_TYPE_JINGLE_TRANSPORT_IFACE))
#define WOCKY_JINGLE_TRANSPORT_IFACE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), WOCKY_TYPE_JINGLE_TRANSPORT_IFACE,\
                              WockyJingleTransportIfaceClass))

void wocky_jingle_transport_iface_parse_candidates (WockyJingleTransportIface *,
    WockyNode *, GError **);

void wocky_jingle_transport_iface_new_local_candidates (
    WockyJingleTransportIface *self,
    GList *candidates);
void wocky_jingle_transport_iface_inject_candidates (
    WockyJingleTransportIface *self,
    WockyNode *transport_node);
void wocky_jingle_transport_iface_send_candidates (
    WockyJingleTransportIface *self,
    gboolean all);
gboolean wocky_jingle_transport_iface_can_accept (
    WockyJingleTransportIface *self);

GList *wocky_jingle_transport_iface_get_remote_candidates (WockyJingleTransportIface *);
GList *wocky_jingle_transport_iface_get_local_candidates (WockyJingleTransportIface *);
WockyJingleTransportType wocky_jingle_transport_iface_get_transport_type (WockyJingleTransportIface *);
gboolean jingle_transport_get_credentials (WockyJingleTransportIface *,
    gchar **ufrag, gchar **pwd);

WockyJingleTransportIface *wocky_jingle_transport_iface_new (
    GType type, WockyJingleContent *content, const gchar *transport_ns);

WockyJingleCandidate *wocky_jingle_candidate_new (WockyJingleTransportProtocol protocol,
    WockyJingleCandidateType type, const gchar *id, int component,
    const gchar *address, int port, int generation, int preference,
    const gchar *username, const gchar *password, int network);

void wocky_jingle_candidate_free (WockyJingleCandidate *c);
void jingle_transport_free_candidates (GList *candidates);


G_END_DECLS

#endif /* #ifndef __WOCKY_JINGLE_TRANSPORT_IFACE_H__ */
