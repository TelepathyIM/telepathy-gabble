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
#include "presence-cache.h"
#include "namespaces.h"
#include "util.h"

#define DEBUG_FLAG GABBLE_DEBUG_PRESENCE

#include "debug.h"

G_DEFINE_TYPE (GabblePresence, gabble_presence, G_TYPE_OBJECT);

#define GABBLE_PRESENCE_PRIV(account) ((account)->priv)

typedef struct _Resource Resource;

struct _Resource {
    gchar *name;
    GabbleCapabilitySet *cap_set;
    guint caps_serial;
    GabblePresenceId status;
    gchar *status_message;
    gint8 priority;
    time_t last_activity;
};

struct _GabblePresencePrivate {
    /* The aggregated caps of all the contacts' resources. */
    GabbleCapabilitySet *cap_set;

    gchar *no_resource_status_message;
    GSList *resources;
    guint olpc_views;
};

static Resource *
_resource_new (gchar *name)
{
  Resource *new = g_slice_new0 (Resource);
  new->name = name;
  new->cap_set = gabble_capability_set_new ();
  new->status = GABBLE_PRESENCE_OFFLINE;
  new->status_message = NULL;
  new->priority = 0;
  new->caps_serial = 0;
  new->last_activity = 0;

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
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);

  for (i = priv->resources; NULL != i; i = i->next)
    _resource_free (i->data);

  g_slist_free (priv->resources);
  gabble_capability_set_free (priv->cap_set);

  g_free (presence->nickname);
  g_free (presence->avatar_sha1);
  g_free (priv->no_resource_status_message);
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
resource_better_than (Resource *a, Resource *b)
{
    if (a->priority < 0)
        return FALSE;

    if (NULL == b)
        return TRUE;

    if (a->status < b->status)
        return FALSE;
    else if (a->status > b->status)
        return TRUE;

    if (a->last_activity < b->last_activity)
        return FALSE;
    else if (a->last_activity > b->last_activity)
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

const gchar *
gabble_presence_pick_resource_by_caps (
    GabblePresence *presence,
    GabbleCapabilitySetPredicate predicate, gconstpointer user_data)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
  GSList *i;
  Resource *chosen = NULL;

  g_return_val_if_fail (presence != NULL, NULL);
  g_return_val_if_fail (predicate != NULL, NULL);

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      if (predicate (res->cap_set, user_data) &&
          (resource_better_than (res, chosen)))
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
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
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
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
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
      if (DEBUGGING)
        {
          gchar *tmp = gabble_capability_set_dump (cap_set, "  ");

          DEBUG ("Setting capabilities for bare JID:\n%s", tmp);
          g_free (tmp);
        }

      gabble_capability_set_update (priv->cap_set, cap_set);
      return;
    }

  if (DEBUGGING)
    {
      gchar *tmp = gabble_capability_set_dump (cap_set, "  ");

      DEBUG ("about to add caps to resource %s with serial %u:\n%s", resource,
          serial, tmp);
      g_free (tmp);
    }

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *tmp = (Resource *) i->data;

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
              DEBUG ("adding caps to resource %s", resource);

              gabble_capability_set_update (tmp->cap_set, cap_set);
            }
        }

      gabble_capability_set_update (priv->cap_set, tmp->cap_set);
    }

  if (DEBUGGING)
    {
      gchar *tmp = gabble_capability_set_dump (priv->cap_set, "  ");

      DEBUG ("Aggregate capabilities are now:\n%s", tmp);
      g_free (tmp);
    }
}

static Resource *
_find_resource (GabblePresence *presence, const gchar *resource)
{
  GSList *i;
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      if (0 == strcmp (res->name, resource))
        return res;
    }

  return NULL;
}

static void
aggregate_resources (GabblePresence *presence)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
  GSList *i;
  guint8 prio;

  /* select the most preferable Resource and update presence->* based on our
   * choice */
  gabble_capability_set_clear (priv->cap_set);
  presence->status = GABBLE_PRESENCE_OFFLINE;

  prio = -128;

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *r = (Resource *) i->data;

      gabble_capability_set_update (priv->cap_set, r->cap_set);

      /* trump existing status & message if it's more present
       * or has the same presence and a higher priority */
      if (r->status > presence->status ||
          (r->status == presence->status && r->priority > prio))
        {
          presence->status = r->status;
          presence->status_message = r->status_message;
          prio = r->priority;
        }
    }

  if (presence->status <= GABBLE_PRESENCE_HIDDEN && priv->olpc_views > 0)
    {
      /* Contact is in at least one view and we didn't receive a better
       * presence from him so announce it as available */
      presence->status = GABBLE_PRESENCE_AVAILABLE;
      g_free (presence->status_message);
      presence->status_message = NULL;
    }
}

gboolean
gabble_presence_update (GabblePresence *presence,
                        const gchar *resource,
                        GabblePresenceId status,
                        const gchar *status_message,
                        gint8 priority)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
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
          res->last_activity = time (NULL);
    }

  /* select the most preferable Resource and update presence->* based on our
   * choice */
  gabble_capability_set_clear (priv->cap_set);
  presence->status = GABBLE_PRESENCE_OFFLINE;

  /* use the status message from any offline Resource we're
   * keeping around just because it has a message on it */
  presence->status_message = res ? res->status_message : NULL;

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
  WockyXmppStanza *stanza)
{
  WockyXmppNode *vcard_node;

  switch (presence->status)
    {
    case GABBLE_PRESENCE_AVAILABLE:
    case GABBLE_PRESENCE_OFFLINE:
    case GABBLE_PRESENCE_HIDDEN:
      break;
    case GABBLE_PRESENCE_AWAY:
      wocky_xmpp_node_add_child_with_content (stanza->node, "show",
          JABBER_PRESENCE_SHOW_AWAY);
      break;
    case GABBLE_PRESENCE_CHAT:
      wocky_xmpp_node_add_child_with_content (stanza->node, "show",
          JABBER_PRESENCE_SHOW_CHAT);
      break;
    case GABBLE_PRESENCE_DND:
      wocky_xmpp_node_add_child_with_content (stanza->node, "show",
          JABBER_PRESENCE_SHOW_DND);
      break;
    case GABBLE_PRESENCE_XA:
      wocky_xmpp_node_add_child_with_content (stanza->node, "show",
          JABBER_PRESENCE_SHOW_XA);
      break;
    default:
      g_critical ("%s: Unexpected Telepathy presence type", G_STRFUNC);
      break;
    }

  if (presence->status_message)
    wocky_xmpp_node_add_child_with_content (stanza->node, "status",
        presence->status_message);

  vcard_node = wocky_xmpp_node_add_child_ns (stanza->node, "x",
        NS_VCARD_TEMP_UPDATE);

  if (presence->avatar_sha1 != NULL)
    {
      wocky_xmpp_node_add_child_with_content (vcard_node, "photo",
        presence->avatar_sha1);
    }
}

LmMessage *
gabble_presence_as_message (GabblePresence *presence,
                            const gchar *to)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
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

  gabble_presence_add_status_and_vcard (presence, WOCKY_XMPP_STANZA (message));

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
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);

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
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (self);
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
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (self);
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
