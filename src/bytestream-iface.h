/*
 * bytestream-iface.h - Header for GabbleBytestream interface
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

#ifndef __GABBLE_BYTESTREAM_IFACE_H__
#define __GABBLE_BYTESTREAM_IFACE_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

G_BEGIN_DECLS

typedef enum
{
  /* Received a SI request, response not yet sent */
  GABBLE_BYTESTREAM_STATE_LOCAL_PENDING = 0,
  /* We accepted SI request.
   * bytestream specific init steps not yet performed */
  GABBLE_BYTESTREAM_STATE_ACCEPTED,
  /* Remote contact accepted the SI request.
   * bytestream specific initiation started */
  GABBLE_BYTESTREAM_STATE_INITIATING,
  /* Bytestream open */
  GABBLE_BYTESTREAM_STATE_OPEN,
  GABBLE_BYTESTREAM_STATE_CLOSED,
  NUM_GABBLE_BYTESTREAM_STATES,
} GabbleBytestreamState;

typedef void (* GabbleBytestreamAugmentSiAcceptReply) (
    LmMessageNode *si, gpointer user_data);

typedef struct _GabbleBytestreamIface GabbleBytestreamIface;
typedef struct _GabbleBytestreamIfaceClass GabbleBytestreamIfaceClass;

struct _GabbleBytestreamIfaceClass {
  GTypeInterface parent;

  gboolean (*initiate) (GabbleBytestreamIface *bytestream);
  gboolean (*send) (GabbleBytestreamIface *bytestream, guint len,
      const gchar *data);
  void (*close) (GabbleBytestreamIface *bytestream, GError *error);
  void (*accept) (GabbleBytestreamIface *bytestream,
      GabbleBytestreamAugmentSiAcceptReply func, gpointer user_data);
  const gchar * (*get_protocol) (GabbleBytestreamIface *bytestream);
};

GType gabble_bytestream_iface_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_BYTESTREAM_IFACE \
  (gabble_bytestream_iface_get_type ())
#define GABBLE_BYTESTREAM_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_BYTESTREAM_IFACE, \
                              GabbleBytestreamIface))
#define GABBLE_IS_BYTESTREAM_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_BYTESTREAM_IFACE))
#define GABBLE_BYTESTREAM_IFACE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GABBLE_TYPE_BYTESTREAM_IFACE,\
                              GabbleBytestreamIfaceClass))

gboolean gabble_bytestream_iface_initiate (GabbleBytestreamIface *bytestream);

gboolean gabble_bytestream_iface_send (GabbleBytestreamIface *bytestream,
    guint len, const gchar *data);

void gabble_bytestream_iface_close (GabbleBytestreamIface *bytestream,
    GError *error);

void gabble_bytestream_iface_accept (GabbleBytestreamIface *bytestream,
    GabbleBytestreamAugmentSiAcceptReply func, gpointer user_data);

const gchar *gabble_bytestream_iface_get_protocol (
    GabbleBytestreamIface *bytestream);

G_END_DECLS

#endif /* #ifndef __GABBLE_BYTESTREAM_IFACE_H__ */
