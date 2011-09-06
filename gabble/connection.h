/*
 * connection.h - connection API available to telepathy-gabble plugins
 * Copyright © 2010 Collabora Ltd.
 * Copyright © 2010 Nokia Corporation
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

#ifndef GABBLE_PLUGINS_CONNECTION_H
#define GABBLE_PLUGINS_CONNECTION_H

#include <telepathy-glib/base-connection.h>

#include <gabble/capabilities-set.h>
#include <gabble/types.h>

#include <wocky/wocky-xep-0115-capabilities.h>

G_BEGIN_DECLS

#define GABBLE_TYPE_CONNECTION (gabble_connection_get_type ())
#define GABBLE_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_CONNECTION, GabbleConnection))
#define GABBLE_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_CONNECTION, \
                           GabbleConnectionClass))
#define GABBLE_IS_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_CONNECTION))
#define GABBLE_IS_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_CONNECTION))
#define GABBLE_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_CONNECTION, \
                              GabbleConnectionClass))

typedef struct _GabbleConnectionClass GabbleConnectionClass;

GType gabble_connection_get_type (void);

void gabble_connection_update_sidecar_capabilities (
    GabbleConnection *connection,
    const GabbleCapabilitySet *add_set,
    const GabbleCapabilitySet *remove_set);
gchar *gabble_connection_add_sidecar_own_caps (
    GabbleConnection *connection,
    const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities) G_GNUC_WARN_UNUSED_RESULT;

WockySession *gabble_connection_get_session (
    GabbleConnection *connection);

gchar *gabble_connection_get_full_jid (GabbleConnection *conn);

const gchar * gabble_connection_get_jid_for_caps (GabbleConnection *conn,
    WockyXep0115Capabilities *caps);

G_END_DECLS

#endif
