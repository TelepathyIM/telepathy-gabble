/*
 * tls-certificate.h - Header for GabbleTLSCertificate
 * Copyright (C) 2010 Collabora Ltd.
 * @author Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
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

#ifndef __GABBLE_TLS_CERTIFICATE_H__
#define __GABBLE_TLS_CERTIFICATE_H__

#include <glib-object.h>

#include <telepathy-glib/dbus-properties-mixin.h>

G_BEGIN_DECLS

typedef struct _GabbleTLSCertificate GabbleTLSCertificate;
typedef struct _GabbleTLSCertificateClass GabbleTLSCertificateClass;
typedef struct _GabbleTLSCertificatePrivate GabbleTLSCertificatePrivate;

struct _GabbleTLSCertificateClass {
  GObjectClass parent_class;

  TpDBusPropertiesMixinClass dbus_props_class;
};

struct _GabbleTLSCertificate {
  GObject parent;

  GabbleTLSCertificatePrivate *priv;
};

GType gabble_tls_certificate_get_type (void);

#define GABBLE_TYPE_TLS_CERTIFICATE \
  (gabble_tls_certificate_get_type ())
#define GABBLE_TLS_CERTIFICATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_TLS_CERTIFICATE, \
      GabbleTLSCertificate))
#define GABBLE_TLS_CERTIFICATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_TLS_CERTIFICATE, \
      GabbleTLSCertificateClass))
#define GABBLE_IS_TLS_CERTIFICATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_TLS_CERTIFICATE))
#define GABBLE_IS_TLS_CERTIFICATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_TLS_CERTIFICATE))
#define GABBLE_TLS_CERTIFICATE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_TLS_CERTIFICATE, \
      GabbleTLSCertificateClass))

G_END_DECLS

#endif /* #ifndef __GABBLE_TLS_CERTIFICATE_H__*/
