/*
 * handle-private.h - private structures for handles.
 *
 * Copyright (C) 2005,2006 Collabora Ltd.
 * Copyright (C) 2005,2006 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __HANDLES_PRIVATE_H__
#define __HANDLE_PRIVATE_H__

/* private data types */

struct _GabbleHandleRepo
{
  GHashTable *handles;
  GabbleHandle list_publish;
  GabbleHandle list_subscribe;
};

typedef struct _GabbleHandlePriv GabbleHandlePriv;

struct _GabbleHandlePriv
{
  guint refcount;
  TpHandleType type;
};

#endif /*__HANDLE_PRIVATE_H__*/

