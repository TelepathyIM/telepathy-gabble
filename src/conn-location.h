
#ifndef __CONN_LOCATION_H__
#define __CONN_LOCATION_H__

#include <extensions/extensions.h>

G_BEGIN_DECLS

void location_iface_init (gpointer g_iface, gpointer iface_data);

void conn_location_propeties_getter (GObject *object, GQuark interface,
    GQuark name, GValue *value, gpointer getter_data);

G_END_DECLS

#endif /* __CONN_LOCATION_H__ */
