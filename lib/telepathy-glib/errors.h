/*
 * tp-errors.h - Header for Telepathy error types
 * Copyright (C) 2005, 2007 Collabora Ltd.
 * Copyright (C) 2005, 2007 Nokia Corporation
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

#ifndef __TP_ERRORS_H__
#define __TP_ERRORS_H__

#include <glib-object.h>

#include <telepathy-glib/_gen/telepathy-errors.h>

G_BEGIN_DECLS

GQuark tp_errors_quark (void);

/**
 * TP_ERRORS:
 *
 * The error domain for the D-Bus errors described in the Telepathy
 * specification.
 */
#define TP_ERRORS tp_errors_quark ()

void tp_g_set_error_invalid_handle_type (guint type, GError **error);
void tp_g_set_error_unsupported_handle_type (guint type, GError **error);

G_END_DECLS

#endif
