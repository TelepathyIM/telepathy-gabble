/* Gateway registration plugin
 *
 * Copyright © 2010 Collabora Ltd. <http://www.collabora.co.uk/>
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

#include <glib-object.h>

#include <gio/gio.h>
#include <wocky/wocky-session.h>

typedef struct _GabbleGatewayPlugin GabbleGatewayPlugin;
typedef struct _GabbleGatewayPluginClass GabbleGatewayPluginClass;
typedef struct _GabbleGatewayPluginPrivate GabbleGatewayPluginPrivate;

struct _GabbleGatewayPlugin {
    GObject parent;
    GabbleGatewayPluginPrivate *priv;
};

struct _GabbleGatewayPluginClass {
    GObjectClass parent;
};

GType gabble_gateway_plugin_get_type (void);

#define GABBLE_TYPE_GATEWAY_PLUGIN \
  (gabble_gateway_plugin_get_type ())
#define GABBLE_GATEWAY_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GABBLE_TYPE_GATEWAY_PLUGIN, \
                               GabbleGatewayPlugin))
#define GABBLE_GATEWAY_PLUGIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GABBLE_TYPE_GATEWAY_PLUGIN, \
                            GabbleGatewayPluginClass))
#define GABBLE_IS_GATEWAY_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GABBLE_TYPE_GATEWAY_PLUGIN))
#define GABBLE_IS_GATEWAY_PLUGIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GABBLE_TYPE_GATEWAY_PLUGIN))
#define GABBLE_GATEWAY_PLUGIN_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_GATEWAY_PLUGIN, \
                              GabbleGatewayPluginClass))

typedef struct _GabbleGatewaySidecar GabbleGatewaySidecar;
typedef struct _GabbleGatewaySidecarClass GabbleGatewaySidecarClass;
typedef struct _GabbleGatewaySidecarPrivate GabbleGatewaySidecarPrivate;

struct _GabbleGatewaySidecar {
    GObject parent;
    GabbleGatewaySidecarPrivate *priv;
};

struct _GabbleGatewaySidecarClass {
    GObjectClass parent;
};

GType gabble_gateway_sidecar_get_type (void);

#define GABBLE_TYPE_GATEWAY_SIDECAR \
  (gabble_gateway_sidecar_get_type ())
#define GABBLE_GATEWAY_SIDECAR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GABBLE_TYPE_GATEWAY_SIDECAR, \
                               GabbleGatewaySidecar))
#define GABBLE_GATEWAY_SIDECAR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GABBLE_TYPE_GATEWAY_SIDECAR, \
                            GabbleGatewaySidecarClass))
#define GABBLE_IS_GATEWAY_SIDECAR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GABBLE_TYPE_GATEWAY_SIDECAR))
#define GABBLE_IS_GATEWAY_SIDECAR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GABBLE_TYPE_GATEWAY_SIDECAR))
#define GABBLE_GATEWAY_SIDECAR_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_GATEWAY_SIDECAR, \
                              GabbleGatewaySidecarClass))
