/*
 * bytestreal-ibb.h - Header for GabbleBytestreamIBB
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

#ifndef __GABBLE_BYTESTREAM_IBB_H__
#define __GABBLE_BYTESTREAM_IBB_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

#include <telepathy-glib/base-connection.h>

G_BEGIN_DECLS

typedef enum
{
  /* Received a SI request, response not yet sent */
  BYTESTREAM_IBB_STATE_LOCAL_PENDING = 0,
  /* We accepted SI request.
   * bytestream specific init steps not yet performed */
  BYTESTREAM_IBB_STATE_ACCEPTED,
  /* Remote contact accepted the SI request.
   * bytestream specific initiation started */
  BYTESTREAM_IBB_STATE_INITIATING,
  /* Bytestream open */
  BYTESTREAM_IBB_STATE_OPEN,
  BYTESTREAM_IBB_STATE_CLOSED,
  LAST_BYTESTREAM_IBB_STATE,
} BytestreamIBBState;

typedef struct _GabbleBytestreamIBB GabbleBytestreamIBB;
typedef struct _GabbleBytestreamIBBClass GabbleBytestreamIBBClass;

struct _GabbleBytestreamIBBClass {
  GObjectClass parent_class;
};

struct _GabbleBytestreamIBB {
  GObject parent;

  gpointer priv;
};

GType gabble_bytestream_ibb_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_BYTESTREAM_IBB \
  (gabble_bytestream_ibb_get_type ())
#define GABBLE_BYTESTREAM_IBB(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_BYTESTREAM_IBB,\
                              GabbleBytestreamIBB))
#define GABBLE_BYTESTREAM_IBB_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_BYTESTREAM_IBB,\
                           GabbleBytestreamIBBClass))
#define GABBLE_IS_BYTESTREAM_IBB(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_BYTESTREAM_IBB))
#define GABBLE_IS_BYTESTREAM_IBB_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_BYTESTREAM_IBB))
#define GABBLE_BYTESTREAM_IBB_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_BYTESTREAM_IBB,\
                              GabbleBytestreamIBBClass))

void
gabble_bytestream_ibb_initiation (GabbleBytestreamIBB *ibb);

gboolean
gabble_bytestream_ibb_send (GabbleBytestreamIBB *ibb, guint len,
   gchar *str);

void
gabble_bytestream_ibb_close (GabbleBytestreamIBB *ibb);

gboolean
gabble_bytestream_ibb_receive (GabbleBytestreamIBB *ibb,
   LmMessage *msg);

LmMessage *
gabble_bytestream_ibb_make_accept_iq (GabbleBytestreamIBB *ibb);

void
gabble_bytestream_ibb_accept (GabbleBytestreamIBB *ibb, LmMessage *msg);

gboolean
gabble_bytestream_ibb_send_to (GabbleBytestreamIBB *ibb, TpHandle to,
   guint len, gchar *str);

G_END_DECLS

#endif /* #ifndef __GABBLE_BYTESTREAM_IBB_H__ */
