/*
 * gtalk-ft-manager.h - Header for GtalkFtManager
 * Copyright (C) 2010 Collabora Ltd.
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

#ifndef __GTALK_FT_MANAGER_H__
#define __GTALK_FT_MANAGER_H__

#include <glib-object.h>
#include "jingle-session.h"

typedef struct _GtalkFtManager GtalkFtManager;

#include "ft-channel.h"

G_BEGIN_DECLS

typedef struct _GtalkFtManagerClass GtalkFtManagerClass;

GType gtalk_ft_manager_get_type (void);

/* TYPE MACROS */
#define GTALK_TYPE_FT_MANAGER \
  (gtalk_ft_manager_get_type ())
#define GTALK_FT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GTALK_TYPE_FT_MANAGER, \
                              GtalkFtManager))
#define GTALK_FT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GTALK_TYPE_FT_MANAGER, \
                           GtalkFtManagerClass))
#define GTALK_IS_FT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GTALK_TYPE_FT_MANAGER))
#define GTALK_IS_FT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GTALK_TYPE_FT_MANAGER))
#define GTALK_FT_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GTALK_TYPE_FT_MANAGER, \
                              GtalkFtManagerClass))

struct _GtalkFtManagerClass {
    GObjectClass parent_class;
};

typedef struct _GtalkFtManagerPrivate GtalkFtManagerPrivate;

struct _GtalkFtManager {
    GObject parent;
    GtalkFtManagerPrivate *priv;
};


#define PENDING 0
#define ACCEPTED 1
#define OPEN 2
#define TERMINATED 3
#define CONNECTION_FAILED 4
#define COMPLETED 5

#define NONE 0
#define LOCAL_STOPPED 1
#define REMOTE_STOPPED 2
#define LOCAL_ERROR 3

GtalkFtManager *gtalk_ft_manager_new (GabbleFileTransferChannel *channel,
    GabbleJingleFactory *jingle_factory, TpHandle handle, const gchar *resource);

GtalkFtManager *gtalk_ft_manager_new_from_session (
    GabbleJingleFactory *jingle_factory,  GabbleJingleSession *session);

void gtalk_ft_manager_initiate (GtalkFtManager *self,
    GabbleFileTransferChannel *channel);
void gtalk_ft_manager_accept (GtalkFtManager *self,
    GabbleFileTransferChannel *channel);
void gtalk_ft_manager_terminate (GtalkFtManager *self,
    GabbleFileTransferChannel * channel);
void gtalk_ft_manager_block_reading (GtalkFtManager *self, gboolean block);
gboolean gtalk_ft_manager_send_data (GtalkFtManager *self,
    const gchar *data, guint length);


#endif /* __GTALK_FT_MANAGER_H__ */

