/*
 * gabble.h - entry point and utility functions for telepathy-gabble
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#ifndef __GABBLE_H__
#define __GABBLE_H__

#include <dbus/dbus-glib.h>

G_BEGIN_DECLS

int gabble_main (int argc, char **argv);

#ifdef ENABLE_DEBUG
void gabble_lm_debug (void);
#else
#define gabble_lm_debug() G_STMT_START { } G_STMT_END
#endif

/* To be used only by tests */
void gabble_init (void);

G_END_DECLS

#endif /* #ifndef __GABBLE_H__*/

