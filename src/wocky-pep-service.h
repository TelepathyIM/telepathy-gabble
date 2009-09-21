/*
 * wocky-pep-service.h - Header of WockyPepService
 * Copyright (C) 2009 Collabora Ltd.
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

#ifndef __WOCKY_PEP_SERVICE_H__
#define __WOCKY_PEP_SERVICE_H__

#include <glib-object.h>
#include <gio/gio.h>
#include <wocky/wocky-xmpp-stanza.h>
#include <wocky/wocky-session.h>

G_BEGIN_DECLS

typedef struct _WockyPepService WockyPepService;
typedef struct _WockyPepServiceClass WockyPepServiceClass;

struct _WockyPepServiceClass {
  GObjectClass parent_class;
};

struct _WockyPepService {
  GObject parent;
};

GType wocky_pep_service_get_type (void);

#define WOCKY_TYPE_PEP_SERVICE \
  (wocky_pep_service_get_type ())
#define WOCKY_PEP_SERVICE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), WOCKY_TYPE_PEP_SERVICE, \
   WockyPepService))
#define WOCKY_PEP_SERVICE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), WOCKY_TYPE_PEP_SERVICE, \
   WockyPepServiceClass))
#define WOCKY_IS_PEP_SERVICE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), WOCKY_TYPE_PEP_SERVICE))
#define WOCKY_IS_PEP_SERVICE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), WOCKY_TYPE_PEP_SERVICE))
#define WOCKY_PEP_SERVICE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), WOCKY_TYPE_PEP_SERVICE, \
   WockyPepServiceClass))

WockyPepService * wocky_pep_service_new (const gchar *node,
    gboolean subscribe);

void wocky_pep_service_start (WockyPepService *pep_service,
    WockySession *session);

void wocky_pep_service_get_async (WockyPepService *pep,
    WockyBareContact *contact,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

WockyXmppStanza * wocky_pep_service_get_finish (WockyPepService *pep,
    GAsyncResult *result,
    GError **error);

WockyXmppStanza * wocky_pep_service_make_publish_stanza (WockyPepService *pep,
    WockyXmppNode **item);

G_END_DECLS

#endif /* __WOCKY_PEP_SERVICE_H__ */
