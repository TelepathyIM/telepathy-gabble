
#ifndef __CONN_LOCATION_H__
#define __CONN_LOCATION_H__

#include "connection.h"
#include <extensions/extensions.h>

G_BEGIN_DECLS

void location_iface_init (gpointer g_iface, gpointer iface_data);

void conn_location_properties_getter (GObject *object, GQuark interface,
    GQuark name, GValue *value, gpointer getter_data);
gboolean conn_location_properties_setter (GObject *object, GQuark interface,
    GQuark name, const GValue *value, gpointer setter_data, GError **error);

gboolean geolocation_event_handler (GabbleConnection *conn,
    LmMessage *msg, TpHandle handle);

void conn_location_init (GabbleConnection *conn);

G_END_DECLS

#endif /* __CONN_LOCATION_H__ */
