/*
 * util.h - Headers for Gabble utility functions
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
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

#ifndef __GABBLE_UTIL_H__
#define __GABBLE_UTIL_H__

#include <config.h>

#include <glib.h>
#include <telepathy-glib/telepathy-glib.h>
#include <wocky/wocky.h>

#ifdef ENABLE_VOIP
#include "jingle-factory.h"
#include "jingle-content.h"
#endif

#include "types.h"

/* Guarantees that the resulting hash is in lower-case */
gchar *sha1_hex (const gchar *bytes, guint len);

/* A SHA1 digest is 20 bytes long */
#define SHA1_HASH_SIZE 20
void sha1_bin (const gchar *bytes, guint len, guchar out[SHA1_HASH_SIZE]);

gchar *gabble_generate_id (void);

void lm_message_node_add_own_nick (WockyNode *node,
    GabbleConnection *conn);

G_GNUC_WARN_UNUSED_RESULT
gchar *gabble_encode_jid (const gchar *node, const gchar *domain,
    const gchar *resource);

gchar *gabble_remove_resource (const gchar *jid);
gchar *gabble_normalize_contact (TpHandleRepoIface *repo, const gchar *jid,
    gpointer userdata, GError **error);
gchar *gabble_normalize_room (TpHandleRepoIface *repo, const gchar *jid,
    gpointer context, GError **error);
TpHandle gabble_get_room_handle_from_jid (TpHandleRepoIface *room_repo,
    const gchar *jid);

GHashTable *lm_message_node_extract_properties (WockyNode *node,
    const gchar *prop);
void
lm_message_node_add_children_from_properties (WockyNode *node,
    GHashTable *properties, const gchar *prop);

void gabble_signal_connect_weak (gpointer instance, const gchar *detailed_signal,
    GCallback c_handler, GObject *user_data);
guint gabble_idle_add_weak (GSourceFunc function, GObject *object);

GPtrArray *gabble_g_ptr_array_copy (GPtrArray *source);

WockyBareContact * ensure_bare_contact_from_jid (GabbleConnection *conn,
    const gchar *jid);
TpHandle ensure_handle_from_contact (
    GabbleConnection *conn,
    WockyContact *contact);

#ifdef ENABLE_VOIP
gboolean jingle_pick_best_resource (GabbleConnection *conn,
    TpHandle peer,
    gboolean want_audio,
    gboolean want_video,
    const char **transport_ns,
    JingleDialect *dialect,
    const gchar **resource_out);

const gchar *jingle_pick_best_content_type (GabbleConnection *conn,
    TpHandle peer,
    const gchar *resource,
    JingleMediaType type);

GPtrArray *gabble_call_candidates_to_array (GList *candidates);
#endif

gchar * gabble_peer_to_jid (GabbleConnection *conn,
    TpHandle peer,
    const gchar *resource);

gboolean
gabble_flag_from_nick (GType flag_type, const gchar *nick,
    guint *value);

void gabble_simple_async_succeed_or_fail_in_idle (gpointer self,
    GAsyncReadyCallback callback,
    gpointer user_data,
    gpointer source_tag,
    const GError *error);

GSimpleAsyncResult *gabble_simple_async_countdown_new (gpointer self,
    GAsyncReadyCallback callback,
    gpointer user_data,
    gpointer source_tag,
    gssize todo);
void gabble_simple_async_countdown_inc (GSimpleAsyncResult *simple);
void gabble_simple_async_countdown_dec (GSimpleAsyncResult *simple);

/* Boilerplate for telling servers which implement XEP-0079 not to store these
 * messages for delivery later. Include it in your call to wocky_stanza_build()
 * like so:
 *
 *    wocky_stanza_build (WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_NONE,
 *       NULL, jid,
 *       '(', "close",
 *         ':', NS_TUBES,
 *         '@', "tube", id_str,
 *       ')',
 *       GABBLE_AMP_DO_NOT_STORE_SPEC,
 *       NULL);
 *
 * Every 1000th user will win a Marshall amplifier!
 */
#define GABBLE_AMP_DO_NOT_STORE_SPEC \
          '(', "amp", \
            ':', NS_AMP, \
            '(', "rule", \
              '@', "condition", "deliver-at", \
              '@', "value", "stored", \
              '@', "action", "error", \
            ')', \
            '(', "rule", \
              '@', "condition", "match-resource", \
              '@', "value", "exact", \
              '@', "action", "error", \
            ')', \
          ')'


#endif /* __GABBLE_UTIL_H__ */
