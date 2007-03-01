
#ifndef __CONN_PRESENCE_H__
#define __CONN_PRESENCE_H__

G_BEGIN_DECLS

void conn_init_presence (GabbleConnection *conn);
void emit_one_presence_update (GabbleConnection *, TpHandle);
void presence_service_iface_init (gpointer g_iface, gpointer iface_data);

G_END_DECLS

#endif /* __CONN_PRESENCE_H__ */

