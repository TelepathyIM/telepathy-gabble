/*
 * google-relay.h - Header for GabbleGoogleRelaySession
 *
 * Copyright (C) 2006-2008 Collabora Ltd.
 * Copyright (C) 2011 Nokia Corporation
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

#ifndef __GABBLE_GOOGLE_RELAY_H__
#define __GABBLE_GOOGLE_RELAY_H__

#include <glib.h>

#include "jingle-factory.h"

G_BEGIN_DECLS

typedef struct _GabbleGoogleRelayResolver GabbleGoogleRelayResolver;

GabbleGoogleRelayResolver * gabble_google_relay_resolver_new (void);
void gabble_google_relay_resolver_destroy (GabbleGoogleRelayResolver *self);
void gabble_google_relay_resolver_resolve (GabbleGoogleRelayResolver *self,
    guint requests_to_do,
    const gchar *server,
    guint16 port,
    const gchar *token,
    GabbleJingleFactoryRelaySessionCb callback,
    gpointer user_data);

G_END_DECLS

#endif  /* __GABBLE_GOOGLE_RELAY_H__ */
