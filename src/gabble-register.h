/*
 * gabble-register.h - Headers for Gabble account registration
 *
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
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

#ifndef __GABBLE_REGISTER_H__
#define __GABBLE_REGISTER_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

G_BEGIN_DECLS

typedef struct _GabbleRegister GabbleRegister;
typedef struct _GabbleRegisterClass GabbleRegisterClass;

GType gabble_register_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_REGISTER \
  (gabble_register_get_type ())
#define GABBLE_REGISTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_REGISTER, GabbleRegister))
#define GABBLE_REGISTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_REGISTER, GabbleRegisterClass))
#define GABBLE_IS_REGISTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_REGISTER))
#define GABBLE_IS_REGISTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_REGISTER))
#define GABBLE_REGISTER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_REGISTER, GabbleRegisterClass))

struct _GabbleRegisterClass {
    GObjectClass parent_class;
};

struct _GabbleRegister {
    GObject parent;
};

GabbleRegister *gabble_register_new (GabbleConnection *conn);
void gabble_register_start (GabbleRegister *reg);

G_END_DECLS

#endif /* #ifndef __GABBLE_REGISTER_H__ */
