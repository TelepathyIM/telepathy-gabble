/*
 * gabble-presence.c - Gabble's per-contact presence structure
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

#include "config.h"
#include "presence.h"

#include <string.h>
#include <telepathy-glib/channel-manager.h>

#include "capabilities.h"
#include "conn-presence.h"
#include "presence-cache.h"
#include "namespaces.h"
#include "util.h"
#include "gabble-enumtypes.h"

#define DEBUG_FLAG GABBLE_DEBUG_PRESENCE

#include "debug.h"

G_DEFINE_TYPE (GabblePresence, gabble_presence, G_TYPE_OBJECT);

typedef struct _Resource Resource;

struct _Resource {
    gchar *name;
    guint client_type;
    GabbleCapabilitySet *cap_set;
    guint caps_serial;
    GabblePresenceId status;
    gchar *status_message;
    gint8 priority;
    /* The last time we saw an available (or chatty! \o\ /o/) presence for
     * this resource.
     */
    time_t last_available;
};

struct _GabblePresencePrivate {
    /* The aggregated caps of all the contacts' resources. */
    GabbleCapabilitySet *cap_set;

    gchar *no_resource_status_message;
    GSList *resources;
    guint olpc_views;

    gchar *active_resource;
};

static Resource *
_resource_new (gchar *name)
{
  Resource *new = g_slice_new0 (Resource);
  new->name = name;
  new->client_type = 0;
  new->cap_set = gabble_capability_set_new ();
  new->status = GABBLE_PRESENCE_OFFLINE;
  new->status_message = NULL;
  new->priority = 0;
  new->caps_serial = 0;
  new->last_available = 0;

  return new;
}

static void
_resource_free (Resource *resource)
{
  g_free (resource->name);
  g_free (resource->status_message);
  gabble_capability_set_free (resource->cap_set);

  g_slice_free (Resource, resource);
}

static void
gabble_presence_finalize (GObject *object)
{
  GSList *i;
  GabblePresence *presence = GABBLE_PRESENCE (object);
  GabblePresencePrivate *priv = presence->priv;

  for (i = priv->resources; NULL != i; i = i->next)
    _resource_free (i->data);

  g_slist_free (priv->resources);
  gabble_capability_set_free (priv->cap_set);

  g_free (presence->nickname);
  g_free (presence->avatar_sha1);
  g_free (priv->no_resource_status_message);
  g_free (priv->active_resource);
}

static void
gabble_presence_class_init (GabblePresenceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (object_class, sizeof (GabblePresencePrivate));
  object_class->finalize = gabble_presence_finalize;
}

static void
gabble_presence_init (GabblePresence *self)
{
  GabblePresencePrivate *priv;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_PRESENCE, GabblePresencePrivate);

  priv = self->priv;
  priv->cap_set = gabble_capability_set_new ();
  priv->resources = NULL;
}

GabblePresence *
gabble_presence_new (void)
{
  return g_object_new (GABBLE_TYPE_PRESENCE, NULL);
}

static gboolean
resource_better_than (
    Resource *a,
    Resource *b,
    GabbleClientType preferred_client_type)
{
    if (a->priority < 0)
        return FALSE;

    if (NULL == b)
        return TRUE;

    if (preferred_client_type != 0)
      {
        gboolean a_p = a->client_type & preferred_client_type;
        gboolean b_p = b->client_type & preferred_client_type;

        if (a_p && !b_p)
          return TRUE;
        if (!a_p && b_p)
          return FALSE;
      }

    if (a->status < b->status)
        return FALSE;
    else if (a->status > b->status)
        return TRUE;

    if (a->last_available < b->last_available)
        return FALSE;
    else if (a->last_available > b->last_available)
        return TRUE;

    return (a->priority > b->priority);
}

gboolean
gabble_presence_has_cap (GabblePresence *presence,
    const gchar *ns)
{
  g_return_val_if_fail (presence != NULL, FALSE);

  return gabble_capability_set_has (presence->priv->cap_set, ns);
}

GabbleCapabilitySet *
gabble_presence_dup_caps (GabblePresence *presence)
{
  g_return_val_if_fail (presence != NULL, NULL);
  return gabble_capability_set_copy (presence->priv->cap_set);
}

const GabbleCapabilitySet *
gabble_presence_peek_caps (GabblePresence *presence)
{
  g_return_val_if_fail (presence != NULL, NULL);
  return presence->priv->cap_set;
}

gboolean
gabble_presence_has_resources (GabblePresence *self)
{
  return (self->priv->resources != NULL);
}

/*
 * gabble_presence_pick_resource_by_caps:
 * @presence: a presence, which may not be NULL
 * @preferred_client_type: a single client type flag, such as
 *  GABBLE_CLIENT_TYPE_PHONE, to specify that resources with that type should
 *  be preferred to those without it; or 0 to express no preference
 * @predicate: a condition which must be satisfied by a resource's
 *  capabilities, or NULL to disregard capabilities
 * @user_data: the second argument for @predicate (ignored if @predicate is
 *  NULL)
 *
 */
const gchar *
gabble_presence_pick_resource_by_caps (
    GabblePresence *presence,
    GabbleClientType preferred_client_type,
    GabbleCapabilitySetPredicate predicate,
    gconstpointer user_data)
{
  GabblePresencePrivate *priv = presence->priv;
  GSList *i;
  Resource *chosen = NULL;

  g_return_val_if_fail (presence != NULL, NULL);

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      if (predicate != NULL && !predicate (res->cap_set, user_data))
        continue;

      if (resource_better_than (res, chosen, preferred_client_type))
        chosen = res;
    }

  if (chosen)
    return chosen->name;
  else
    return NULL;
}

gboolean
gabble_presence_resource_has_caps (GabblePresence *presence,
                                   const gchar *resource,
                                   GabbleCapabilitySetPredicate predicate,
                                   gconstpointer user_data)
{
  GabblePresencePrivate *priv = presence->priv;
  GSList *i;

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      if (!tp_strdiff (res->name, resource))
        return predicate (res->cap_set, user_data);
    }

  return FALSE;
}

void
gabble_presence_set_capabilities (GabblePresence *presence,
                                  const gchar *resource,
                                  const GabbleCapabilitySet *cap_set,
                                  guint serial)
{
  GabblePresencePrivate *priv = presence->priv;
  GSList *i;

  if (resource == NULL && priv->resources != NULL)
    {
      /* This is consistent with the handling of presence: if we get presence
       * from a bare JID, we throw away all the resources, and if we get
       * presence from a resource, any presence we stored from a bare JID is
       * overridden by the aggregated presence.
       */
      DEBUG ("Ignoring caps for NULL resource since we have presence for "
        "some resources");
      return;
    }

  gabble_capability_set_clear (priv->cap_set);

  if (resource == NULL)
    {
      DEBUG ("Setting capabilities for bare JID");
      gabble_capability_set_update (priv->cap_set, cap_set);
      return;
    }

  DEBUG ("about to add caps to resource %s with serial %u", resource, serial);

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *tmp = (Resource *) i->data;

      /* This does not use _find_resource() because it also refreshes
       * priv->cap_set as we go.
       */
      if (0 == strcmp (tmp->name, resource))
        {
          DEBUG ("found resource %s", resource);

          if (serial > tmp->caps_serial)
            {
              DEBUG ("new serial %u, old %u, clearing caps", serial,
                tmp->caps_serial);
              tmp->caps_serial = serial;
              gabble_capability_set_clear (tmp->cap_set);
            }

          if (serial >= tmp->caps_serial)
            {
              DEBUG ("updating caps for resource %s", resource);

              gabble_capability_set_update (tmp->cap_set, cap_set);
            }
        }

      gabble_capability_set_update (priv->cap_set, tmp->cap_set);
    }
}

static Resource *
_find_resource (GabblePresence *presence, const gchar *resource)
{
  GSList *i;

  /* you've been warned! */
  g_return_val_if_fail (presence != NULL, NULL);
  g_return_val_if_fail (resource != NULL, NULL);

  for (i = presence->priv->resources; NULL != i; i = i->next)
    {
      Resource *res = i->data;

      if (!tp_strdiff (res->name, resource))
        return res;
    }

  return NULL;
}

static gboolean
aggregate_resources (GabblePresence *presence)
{
  GabblePresencePrivate *priv = presence->priv;
  GSList *i;
  Resource *best = NULL;
  guint old_client_types = presence->client_types;

  /* select the most preferable Resource and update presence->* based on our
   * choice */
  gabble_capability_set_clear (priv->cap_set);
  presence->status = GABBLE_PRESENCE_OFFLINE;

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *r = (Resource *) i->data;

      gabble_capability_set_update (priv->cap_set, r->cap_set);

      /* This doesn't use resource_better_than() because phone preferences take
       * priority above all others whereas this is only using the PC thing as a
       * last-ditch tiebreak. wjt looked into changing this but gave up because
       * it's messy and the phone preference stuff will go away when we do
       * Jingle call forking anyway:
       * <https://bugs.freedesktop.org/show_bug.cgi?id=26673>
       */

      /* trump existing status & message if it's more present
       * or has the same presence and was more recently available
       * or has the same presence and a higher priority */
      if (best == NULL ||
          r->status > best->status ||
          (r->status == best->status &&
              (r->last_available > best->last_available ||
               r->priority > best->priority)) ||
          (r->client_type & GABBLE_CLIENT_TYPE_PC
              && !(best->client_type & GABBLE_CLIENT_TYPE_PC)))
        best = r;
    }

  if (best != NULL)
    {
      presence->status = best->status;
      presence->status_message = best->status_message;
      presence->client_types = best->client_type;

      g_free (priv->active_resource);
      priv->active_resource = g_strdup (best->name);
    }

  if (presence->status <= GABBLE_PRESENCE_HIDDEN && priv->olpc_views > 0)
    {
      /* Contact is in at least one view and we didn't receive a better
       * presence from him so announce it as available */
      presence->status = GABBLE_PRESENCE_AVAILABLE;
      g_free (presence->status_message);
      presence->status_message = NULL;
    }

  return old_client_types != presence->client_types;
}

gboolean
gabble_presence_update (GabblePresence *presence,
                        const gchar *resource,
                        GabblePresenceId status,
                        const gchar *status_message,
                        gint8 priority,
                        gboolean *update_client_types)
{
  GabblePresencePrivate *priv = presence->priv;
  Resource *res;
  GabblePresenceId old_status;
  gchar *old_status_message;
  GSList *i;
  gboolean ret = FALSE;

  /* save our current state */
  old_status = presence->status;
  old_status_message = g_strdup (presence->status_message);

  if (NULL == resource)
    {
      /* presence from a JID with no resource: free all resources and set
       * presence directly */

      for (i = priv->resources; i; i = i->next)
        _resource_free (i->data);

      g_slist_free (priv->resources);
      priv->resources = NULL;

      if (tp_strdiff (priv->no_resource_status_message, status_message))
        {
          g_free (priv->no_resource_status_message);
          priv->no_resource_status_message = g_strdup (status_message);
        }

      presence->status = status;
      presence->status_message = priv->no_resource_status_message;
      goto OUT;
    }

  res = _find_resource (presence, resource);

  /* remove, create or update a Resource as appropriate */
  if (status <= GABBLE_PRESENCE_LAST_UNAVAILABLE)
    {
      if (NULL != res)
        {
          priv->resources = g_slist_remove (priv->resources, res);
          _resource_free (res);
          res = NULL;

          /* recalculate aggregate capability mask */
          gabble_capability_set_clear (priv->cap_set);

          for (i = priv->resources; i; i = i->next)
            {
              Resource *r = (Resource *) i->data;

              gabble_capability_set_update (priv->cap_set, r->cap_set);
            }
        }
    }
  else
    {
      if (NULL == res)
        {
          res = _resource_new (g_strdup (resource));
          priv->resources = g_slist_append (priv->resources, res);
        }

      res->status = status;

      if (tp_strdiff (res->status_message, status_message))
        {
          g_free (res->status_message);
          res->status_message = g_strdup (status_message);
        }

      res->priority = priority;

      if (res->status >= GABBLE_PRESENCE_AVAILABLE)
          res->last_available = time (NULL);
    }

  /* select the most preferable Resource and update presence->* based on our
   * choice */
  gabble_capability_set_clear (priv->cap_set);
  presence->status = GABBLE_PRESENCE_OFFLINE;

  /* use the status message from any offline Resource we're
   * keeping around just because it has a message on it */
  presence->status_message = res ? res->status_message : NULL;

  if (update_client_types != NULL)
    *update_client_types = aggregate_resources (presence);
  else
    aggregate_resources (presence);

OUT:
  /* detect changes */
  if (presence->status != old_status ||
      tp_strdiff (presence->status_message, old_status_message))
    ret = TRUE;

  g_free (old_status_message);
  return ret;
}

void
gabble_presence_add_status_and_vcard (GabblePresence *presence,
  WockyStanza *stanza)
{
  WockyNode *node = wocky_stanza_get_top_node (stanza);
  WockyNode *vcard_node;

  switch (presence->status)
    {
    case GABBLE_PRESENCE_AVAILABLE:
    case GABBLE_PRESENCE_OFFLINE:
    case GABBLE_PRESENCE_HIDDEN:
      break;
    case GABBLE_PRESENCE_AWAY:
      wocky_node_add_child_with_content (node, "show",
          JABBER_PRESENCE_SHOW_AWAY);
      break;
    case GABBLE_PRESENCE_CHAT:
      wocky_node_add_child_with_content (node, "show",
          JABBER_PRESENCE_SHOW_CHAT);
      break;
    case GABBLE_PRESENCE_DND:
      wocky_node_add_child_with_content (node, "show",
          JABBER_PRESENCE_SHOW_DND);
      break;
    case GABBLE_PRESENCE_XA:
      wocky_node_add_child_with_content (node, "show",
          JABBER_PRESENCE_SHOW_XA);
      break;
    default:
      {
        /* FIXME: we're almost duplicate the add_child code here,
         * and we're calling into conn-presence which is not nice.
         */
        TpConnectionPresenceType presence_type =
            conn_presence_get_type (presence);

        switch (presence_type)
          {
          case TP_CONNECTION_PRESENCE_TYPE_AVAILABLE:
          case TP_CONNECTION_PRESENCE_TYPE_OFFLINE:
          case TP_CONNECTION_PRESENCE_TYPE_HIDDEN:
            break;
          case TP_CONNECTION_PRESENCE_TYPE_AWAY:
            wocky_node_add_child_with_content (node, "show",
                JABBER_PRESENCE_SHOW_AWAY);
            break;
          case TP_CONNECTION_PRESENCE_TYPE_BUSY:
            wocky_node_add_child_with_content (node, "show",
                JABBER_PRESENCE_SHOW_DND);
            break;
          case TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY:
            wocky_node_add_child_with_content (node, "show",
                JABBER_PRESENCE_SHOW_XA);
            break;
          default:
            g_critical ("%s: Unexpected Telepathy presence type: %d", G_STRFUNC,
                presence_type);
            break;
          }
      }
    }

  if (presence->status_message)
    wocky_node_add_child_with_content (node, "status",
        presence->status_message);

  vcard_node = wocky_node_add_child_ns (node, "x",
        NS_VCARD_TEMP_UPDATE);

  if (presence->avatar_sha1 != NULL)
    {
      wocky_node_add_child_with_content (vcard_node, "photo",
        presence->avatar_sha1);
    }
}

LmMessage *
gabble_presence_as_message (GabblePresence *presence,
                            const gchar *to)
{
  GabblePresencePrivate *priv = presence->priv;
  LmMessage *message;
  LmMessageSubType subtype;
  Resource *res = priv->resources->data; /* pick first resource */

  g_assert (NULL != res);

  if (presence->status == GABBLE_PRESENCE_OFFLINE)
    subtype = LM_MESSAGE_SUB_TYPE_UNAVAILABLE;
  else
    subtype = LM_MESSAGE_SUB_TYPE_AVAILABLE;

  message = lm_message_new_with_sub_type (to, LM_MESSAGE_TYPE_PRESENCE,
              subtype);

  gabble_presence_add_status_and_vcard (presence, message);

  if (res->priority)
    {
      gchar *priority = g_strdup_printf ("%d", res->priority);
      LmMessageNode *node;

      node = lm_message_get_node (message);
      lm_message_node_add_child (node, "priority", priority);
      g_free (priority);
    }

  return message;
}

gchar *
gabble_presence_dump (GabblePresence *presence)
{
  GSList *i;
  GString *ret = g_string_new ("");
  gchar *tmp;
  GabblePresencePrivate *priv = presence->priv;

  g_string_append_printf (ret,
    "nickname: %s\n"
    "accumulated status: %d\n"
    "accumulated status msg: %s\n"
    "kept while unavailable: %d\n",
    presence->nickname, presence->status,
    presence->status_message,
    presence->keep_unavailable);

  if (priv->cap_set != NULL)
    {
      tmp = gabble_capability_set_dump (priv->cap_set, "  ");
      g_string_append (ret, "capabilities:\n");
      g_string_append (ret, tmp);
      g_free (tmp);
    }

  g_string_append_printf (ret, "resources:\n");

  for (i = priv->resources; i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      g_string_append_printf (ret,
        "  %s\n"
        "    status: %d\n"
        "    status msg: %s\n"
        "    priority: %d\n", res->name, res->status,
        res->status_message, res->priority);

      if (res->cap_set != NULL)
        {
          tmp = gabble_capability_set_dump (res->cap_set, "        ");
          g_string_append (ret, "    capabilities:\n");
          g_string_append (ret, tmp);
          g_free (tmp);
        }
    }

  if (priv->resources == NULL)
    g_string_append_printf (ret, "  (none)\n");

  return g_string_free (ret, FALSE);
}

gboolean
gabble_presence_added_to_view (GabblePresence *self)
{
  GabblePresencePrivate *priv = self->priv;
  GabblePresenceId old_status;
  gchar *old_status_message;
  gboolean ret = FALSE;

  /* save our current state */
  old_status = self->status;
  old_status_message = g_strdup (self->status_message);

  priv->olpc_views++;
  aggregate_resources (self);

  /* detect changes */
  if (self->status != old_status ||
      tp_strdiff (self->status_message, old_status_message))
    ret = TRUE;

  g_free (old_status_message);
  return ret;
}

gboolean
gabble_presence_removed_from_view (GabblePresence *self)
{
  GabblePresencePrivate *priv = self->priv;
  GabblePresenceId old_status;
  gchar *old_status_message;
  gboolean ret = FALSE;

  /* save our current state */
  old_status = self->status;
  old_status_message = g_strdup (self->status_message);

  priv->olpc_views--;
  aggregate_resources (self);

  /* detect changes */
  if (self->status != old_status ||
      tp_strdiff (self->status_message, old_status_message))
    ret = TRUE;

  g_free (old_status_message);
  return ret;
}

gconstpointer
gabble_presence_resource_pick_best_feature (GabblePresence *presence,
    const gchar *resource,
    const GabbleFeatureFallback *table,
    GabbleCapabilitySetPredicate predicate)
{
  Resource *res;
  const GabbleFeatureFallback *row;

  g_return_val_if_fail (presence != NULL, NULL);
  g_return_val_if_fail (resource != NULL, NULL);
  g_return_val_if_fail (predicate != NULL, NULL);
  g_return_val_if_fail (table != NULL, NULL);

  res = _find_resource (presence, resource);

  if (res == NULL)
    return NULL;

  for (row = table; row->result != NULL; row++)
    {
      if (row->considered && predicate (res->cap_set, row->check_data))
        {
          return row->result;
        }
    }

  return NULL;
}

gconstpointer
gabble_presence_pick_best_feature (GabblePresence *presence,
    const GabbleFeatureFallback *table,
    GabbleCapabilitySetPredicate predicate)
{
  const GabbleFeatureFallback *row;

  g_return_val_if_fail (presence != NULL, NULL);
  g_return_val_if_fail (predicate != NULL, NULL);
  g_return_val_if_fail (table != NULL, NULL);

  for (row = table; row->result != NULL; row++)
    {
      if (row->considered && predicate (presence->priv->cap_set,
            row->check_data))
        {
          return row->result;
        }
    }

  return NULL;
}

gboolean
gabble_presence_update_client_types (GabblePresence *presence,
    const gchar *resource,
    guint client_types)
{
  Resource *res = _find_resource (presence, resource);

  if (res == NULL)
    return FALSE;

  res->client_type = client_types;
  return aggregate_resources (presence);
}

GPtrArray *
gabble_presence_get_client_types_array (GabblePresence *presence,
    gboolean add_null,
    const char **resource_name)
{
  GPtrArray *array;
  GFlagsClass *klass;
  GFlagsValue *value;
  guint i;

  array = g_ptr_array_new_with_free_func (g_free);

  klass = g_type_class_ref (GABBLE_TYPE_CLIENT_TYPE);

  if (klass != NULL)
    {
      for (i = 0; i < klass->n_values; i++)
        {
          value = &klass->values[i];

          if (presence->client_types & value->value)
            g_ptr_array_add (array, g_strdup (value->value_nick));
        }

      g_type_class_unref (klass);
    }

  if (add_null)
    g_ptr_array_add (array, NULL);

  if (resource_name != NULL)
    *resource_name = presence->priv->active_resource;

  return array;
}
