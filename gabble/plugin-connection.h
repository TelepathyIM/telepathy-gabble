/*
 * plugin-connection.h — Connection API available to telepathy-gabble plugins
 * Copyright © 2012 Collabora Ltd.
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

#ifndef GABBLE_PLUGIN_CONNECTION_H
#define GABBLE_PLUGIN_CONNECTION_H

#include <glib-object.h>

#include <telepathy-glib/telepathy-glib.h>

#include <gabble/capabilities-set.h>
#include <gabble/types.h>

#include <wocky/wocky.h>

G_BEGIN_DECLS

typedef struct _GabblePluginConnection GabblePluginConnection;
typedef struct _GabblePluginConnectionInterface GabblePluginConnectionInterface;

#define GABBLE_TYPE_PLUGIN_CONNECTION (gabble_plugin_connection_get_type ())
#define GABBLE_PLUGIN_CONNECTION(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), GABBLE_TYPE_PLUGIN_CONNECTION, \
                               GabblePluginConnection))
#define GABBLE_IS_PLUGIN_CONNECTION(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), GABBLE_TYPE_PLUGIN_CONNECTION))
#define GABBLE_PLUGIN_CONNECTION_GET_IFACE(o) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((o), GABBLE_TYPE_PLUGIN_CONNECTION, \
                                  GabblePluginConnectionInterface))

GType gabble_plugin_connection_get_type (void) G_GNUC_CONST;

typedef gchar * (*GabblePluginConnectionAddSidecarCapsFunc) (
    GabblePluginConnection *connection_service,
    const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities);

typedef gchar * (*GabblePluginConnectionAddSidecarCapsFullFunc) (
    GabblePluginConnection *plugin_connection,
    const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities,
    GPtrArray *data_forms);

typedef WockySession * (*GabblePluginConnectionGetSessionFunc) (
    GabblePluginConnection *plugin_connection);

typedef gchar *(*GabblePluginConnectionGetFullJidFunc) (
    GabblePluginConnection *plugin_connection);

typedef const gchar * (*GabblePluginConnectionGetJidForCapsFunc) (
    GabblePluginConnection *plugin_connection,
    WockyXep0115Capabilities *caps);

typedef const gchar* (*GabblePluginConnectionPickBestResourceForCaps) (
    GabblePluginConnection *plugin_connection,
    const gchar *jid,
    GabbleCapabilitySetPredicate predicate,
    gconstpointer user_data);

typedef TpBaseContactList * (*GabblePluginConnectionGetContactList) (
    GabblePluginConnection *plugin_connection);

typedef WockyXep0115Capabilities * (*GabblePluginConnectionGetCaps) (
    GabblePluginConnection *plugin_connection,
    TpHandle handle);

struct _GabblePluginConnectionInterface
{
  GTypeInterface parent;
  GabblePluginConnectionAddSidecarCapsFunc add_sidecar_own_caps;
  GabblePluginConnectionAddSidecarCapsFullFunc add_sidecar_own_caps_full;
  GabblePluginConnectionGetSessionFunc get_session;
  GabblePluginConnectionGetFullJidFunc get_full_jid;
  GabblePluginConnectionGetJidForCapsFunc get_jid_for_caps;
  GabblePluginConnectionPickBestResourceForCaps pick_best_resource_for_caps;
  GabblePluginConnectionGetContactList get_contact_list;
  GabblePluginConnectionGetCaps get_caps;
};

gchar *gabble_plugin_connection_add_sidecar_own_caps (
    GabblePluginConnection *plugin_service,
    const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities);

gchar *gabble_plugin_connection_add_sidecar_own_caps_full (
    GabblePluginConnection *plugin_connection,
    const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities,
    GPtrArray *data_forms) G_GNUC_WARN_UNUSED_RESULT;

WockySession *gabble_plugin_connection_get_session (
    GabblePluginConnection *plugin_connection);

gchar *gabble_plugin_connection_get_full_jid (GabblePluginConnection *conn);

const gchar *gabble_plugin_connection_get_jid_for_caps (
    GabblePluginConnection *plugin_connection,
    WockyXep0115Capabilities *caps);

const gchar *gabble_plugin_connection_pick_best_resource_for_caps (
    GabblePluginConnection *plugin_connection,
    const gchar *jid,
    GabbleCapabilitySetPredicate predicate,
    gconstpointer user_data);

TpBaseContactList *gabble_plugin_connection_get_contact_list (
    GabblePluginConnection *plugin_connection);

WockyXep0115Capabilities *gabble_plugin_connection_get_caps (
    GabblePluginConnection *plugin_connection,
    TpHandle handle);

G_END_DECLS

#endif
