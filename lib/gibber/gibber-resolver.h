/*
 * gibber-resolver.h - Header for GibberResolver
 * Copyright (C) 2006 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd@luon.net>
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

#ifndef __GIBBER_RESOLVER_H__
#define __GIBBER_RESOLVER_H__

#include <glib-object.h>

#include <sys/socket.h>
#include <netdb.h>

G_BEGIN_DECLS

GQuark gibber_resolver_error_quark (void);
#define GIBBER_RESOLVER_ERROR \
  gibber_resolver_error_quark ()

typedef enum {
  /* Invalid or unsupported arguments */
  GIBBER_RESOLVER_ERROR_INVALID_ARGUMENT,
  /* Temperary failure in name resolving */
  GIBBER_RESOLVER_ERROR_RESOLVE_TEMPORARY_FAILURE,
  /* Failed to resolve */
  GIBBER_RESOLVER_ERROR_RESOLVE_FAILURE,
  /* Failed to allocate memory or overflow */
  GIBBER_RESOLVER_ERROR_MEMORY,
  /* Unknown error */
  GIBBER_RESOLVER_ERROR_UNKNOWN,
} GibberResolverError;

typedef enum {
  GIBBER_RESOLVER_SERVICE_TYPE_UDP,
  GIBBER_RESOLVER_SERVICE_TYPE_TCP
} GibberResolverServiceType;

typedef struct _GibberResolver GibberResolver;
typedef struct _GibberResolverClass GibberResolverClass;

struct _GibberResolverClass {
  GObjectClass parent_class;

  gboolean (*resolv_srv) (GibberResolver *resolver, guint id,
     const gchar *service_name, const char *service,
     GibberResolverServiceType type);
  gboolean (*resolv_addrinfo) (GibberResolver *resolver, guint id,
    const gchar *hostname, const char *port, int address_family, int sock_type,
    int protocol, int flags);
  gboolean (*resolv_nameinfo) (GibberResolver *resolver, guint id,
    const struct sockaddr *sa, socklen_t salen, gint flags);
  void (*resolv_cancel) (GibberResolver *resolver, guint id);
};

struct _GibberResolver {
  GObject parent;
};

GType gibber_resolver_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_RESOLVER \
  (gibber_resolver_get_type ())
#define GIBBER_RESOLVER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_RESOLVER, GibberResolver))
#define GIBBER_RESOLVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_RESOLVER, GibberResolverClass))
#define GIBBER_IS_RESOLVER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_RESOLVER))
#define GIBBER_IS_RESOLVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_RESOLVER))
#define GIBBER_RESOLVER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_RESOLVER, GibberResolverClass))

GibberResolver * gibber_resolver_get_resolver (void);
void gibber_resolver_set_resolver (GType object_type);

typedef struct {
  gint address_family;
  gint socket_type;
  gint protocol;
  struct sockaddr_storage sockaddr;
  gsize sockaddr_len;
} GibberResolverAddrInfo;

typedef struct {
  gchar *hostname;
  guint16 port;
  guint16 priority;
  guint16 weight;
} GibberResolverSrvRecord;

GibberResolverAddrInfo * gibber_resolver_addrinfo_new (gint address_family,
    gint socket_type, gint protocol, struct sockaddr *addr,
    gsize sockaddr_len);

void gibber_resolver_addrinfo_free (GibberResolverAddrInfo *addrinfo);

void gibber_resolver_addrinfo_list_free (GList *addrinfo_list);

GibberResolverSrvRecord * gibber_resolver_srv_record_new (gchar *hostname,
  guint16 port, guint16 priority, guint16 weight);

void gibber_resolver_srv_free (GibberResolverSrvRecord *srvrecord);

void gibber_resolver_srv_list_free (GList *srv_list);

typedef void (* gibber_resolver_srv_cb) (GibberResolver *resolver,
  GList *srv_list, GError *error, gpointer user_data, GObject *weak_object);

guint gibber_resolver_srv (GibberResolver *resolver,
  const gchar *service_name, const char *service,
  GibberResolverServiceType type,
  gibber_resolver_srv_cb callback,
  gpointer user_data, GDestroyNotify destroy, GObject *weak_object);

/* entries is a GList of GibberResolverAddrInfo */
typedef void (* gibber_resolver_addrinfo_cb) (GibberResolver *resolver,
  GList *entries, GError *error, gpointer user_data, GObject *weak_object);

guint gibber_resolver_addrinfo (GibberResolver *resolver,
  const gchar *hostname, const char *port,
  int address_family, int sock_type, int protocol, int flags,
  gibber_resolver_addrinfo_cb callback,
  gpointer user_data, GDestroyNotify destroy, GObject *weak_object);

typedef void (* gibber_resolver_nameinfo_cb) (GibberResolver *resolver,
  const gchar *host, const gchar *port, GError *error,
  gpointer user_data, GObject *weak_object);

guint gibber_resolver_nameinfo (GibberResolver *resolver,
  const struct sockaddr *sa, socklen_t salen, gint flags,
  gibber_resolver_nameinfo_cb callback,
  gpointer user_data, GDestroyNotify destroy, GObject *weak_object);

void gibber_resolver_cancel (GibberResolver *resolver, guint id);

gboolean gibber_resolver_sockaddr_to_str (const struct sockaddr *sa,
  gsize salen, gchar **address, gchar **port, GError **error);

/* Utility function for classed implementing GibberResolver */
void gibber_resolver_set_data (GibberResolver *resolver, guint id,
  gpointer data);
gpointer gibber_resolver_get_data (GibberResolver *resolver, guint id);

void gibber_resolver_srv_result (GibberResolver *resolver, guint jobid,
  GList *srv_list, GError *error);

void gibber_resolver_addrinfo_result (GibberResolver *resolver, guint jobid,
  GList *entries, GError *error);

void gibber_resolver_nameinfo_result (GibberResolver *resolver, guint jobid,
  const gchar *hostname, const gchar *port, GError *error);

GList *gibber_resolver_res_query_to_list (guchar *answer, int length);
GError *gibber_resolver_gai_error_to_g_error (int error);
GError *gibber_resolver_h_error_to_g_error (int error);

G_END_DECLS

#endif /* #ifndef __GIBBER_RESOLVER_H__*/
