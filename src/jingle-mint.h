/*
 * jingle-mint.h - creates and configures a WockyJingleFactory
 * Copyright ©2012 Collabora Ltd.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef GABBLE_JINGLE_MINT_H
#define GABBLE_JINGLE_MINT_H

#include <glib-object.h>
#include "types.h"

#include "jingle-info.h"
#include "jingle-types.h"

typedef struct _GabbleJingleMint GabbleJingleMint;
typedef struct _GabbleJingleMintClass GabbleJingleMintClass;
typedef struct _GabbleJingleMintPrivate GabbleJingleMintPrivate;

struct _GabbleJingleMintClass {
    GObjectClass parent_class;
};

struct _GabbleJingleMint {
    GObject parent;

    GabbleJingleMintPrivate *priv;
};

GType gabble_jingle_mint_get_type (void);

GabbleJingleMint *gabble_jingle_mint_new (
    GabbleConnection *connection);

WockyJingleFactory *gabble_jingle_mint_get_factory (
    GabbleJingleMint *self);
WockyJingleInfo *gabble_jingle_mint_get_info (
    GabbleJingleMint *self);

/* TYPE MACROS */
#define GABBLE_TYPE_JINGLE_MINT \
  (gabble_jingle_mint_get_type ())
#define GABBLE_JINGLE_MINT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_JINGLE_MINT, GabbleJingleMint))
#define GABBLE_JINGLE_MINT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_JINGLE_MINT,\
                           GabbleJingleMintClass))
#define GABBLE_IS_JINGLE_MINT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_JINGLE_MINT))
#define GABBLE_IS_JINGLE_MINT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_JINGLE_MINT))
#define GABBLE_JINGLE_MINT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_JINGLE_MINT, \
                              GabbleJingleMintClass))

#endif /* GABBLE_JINGLE_MINT_H */
