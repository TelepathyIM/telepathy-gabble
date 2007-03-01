
#ifndef __AVATARS_H__
#define __AVATARS_H__

#include "gabble-connection.h"

G_BEGIN_DECLS

void conn_init_avatars (GabbleConnection *conn);
void avatars_service_iface_init (gpointer g_iface, gpointer iface_data);

G_END_DECLS

#endif /* __AVATARS_H__ */

