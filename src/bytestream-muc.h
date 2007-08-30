/*
 * bytestream-muc.h - Header for GabbleBytestreamMuc
 * Copyright (C) 2007 Collabora Ltd.
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

#ifndef __GABBLE_BYTESTREAM_MUC_H__
#define __GABBLE_BYTESTREAM_MUC_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

#include <telepathy-glib/base-connection.h>

G_BEGIN_DECLS

typedef struct _GabbleBytestreamMuc GabbleBytestreamMuc;
typedef struct _GabbleBytestreamMucClass GabbleBytestreamMucClass;

struct _GabbleBytestreamMucClass {
  GObjectClass parent_class;
};

struct _GabbleBytestreamMuc {
  GObject parent;

  gpointer priv;
};

GType gabble_bytestream_muc_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_BYTESTREAM_MUC \
  (gabble_bytestream_muc_get_type ())
#define GABBLE_BYTESTREAM_MUC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_BYTESTREAM_MUC,\
                              GabbleBytestreamMuc))
#define GABBLE_BYTESTREAM_MUC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_BYTESTREAM_MUC,\
                           GabbleBytestreamMucClass))
#define GABBLE_IS_BYTESTREAM_MUC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_BYTESTREAM_MUC))
#define GABBLE_IS_BYTESTREAM_MUC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_BYTESTREAM_MUC))
#define GABBLE_BYTESTREAM_MUC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_BYTESTREAM_MUC,\
                              GabbleBytestreamMucClass))

void gabble_bytestream_muc_receive (GabbleBytestreamMuc *bytestream,
   LmMessage *msg);

gboolean
gabble_bytestream_muc_send_to (GabbleBytestreamMuc *bytestream, TpHandle to,
   guint len, gchar *str);

G_END_DECLS

#endif /* #ifndef __GABBLE_BYTESTREAM_MUC_H__ */
