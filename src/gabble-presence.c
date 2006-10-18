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

#include <string.h>
#include <glib.h>

#include "gabble-presence-cache.h"
#include "gabble-presence.h"
#include "util.h"

G_DEFINE_TYPE (GabblePresence, gabble_presence, G_TYPE_OBJECT);

#define GABBLE_PRESENCE_PRIV(account) ((GabblePresencePrivate *)account->priv)

typedef struct _Resource Resource;

struct _Resource {
    gchar *name;
    GabblePresenceCapabilities caps;
    guint caps_serial;
    GabblePresenceId status;
    gchar *status_message;
    gint8 priority;
};

typedef struct _GabblePresencePrivate GabblePresencePrivate;

struct _GabblePresencePrivate {
    gchar *no_resource_status_message;
    GSList *resources;
};

static Resource *
_resource_new (gchar *name)
{
  Resource *new = g_new (Resource, 1);
  new->name = name;
  new->caps = PRESENCE_CAP_NONE;
  new->status = GABBLE_PRESENCE_OFFLINE;
  new->status_message = NULL;
  new->priority = 0;
  return new;
}

static void
_resource_free (Resource *resource)
{
  g_free (resource->status_message);
  g_free (resource);
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
  g_free (presence->nickname);
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
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_PRESENCE, GabblePresencePrivate);
  ((GabblePresencePrivate *)self->priv)->resources = NULL;
}

GabblePresence*
gabble_presence_new (void)
{
  return g_object_new (GABBLE_TYPE_PRESENCE, NULL);
}

const gchar *
gabble_presence_pick_resource_by_caps (
    GabblePresence *presence,
    GabblePresenceCapabilities caps)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
  GSList *i;
  Resource *chosen = NULL;

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      if ((res->priority >= 0) &&
          ((res->caps & caps) == caps) &&
          (NULL == chosen || res->priority > chosen->priority))
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
                                   GabblePresenceCapabilities caps)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
  GSList *i;

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      if (!g_strdiff (res->name, resource) && (res->caps & caps))
        return TRUE;
    }

  return FALSE;
}

void
gabble_presence_set_capabilities (GabblePresence *presence,
                                  const gchar *resource,
                                  GabblePresenceCapabilities caps,
                                  guint serial)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
  GSList *i;

  presence->caps = 0;

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *tmp = (Resource *) i->data;

      if (0 == strcmp (tmp->name, resource))
        {
          if (tmp->caps == 0 || tmp->caps_serial != serial)
            {
              tmp->caps = 0;
              tmp->caps_serial = serial;
            }
          tmp->caps |= caps;
        }

      presence->caps |= tmp->caps;
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
  guint8 prio;
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

      if (g_strdiff (priv->no_resource_status_message, status_message))
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
  if (GABBLE_PRESENCE_OFFLINE == status &&
      NULL == status_message)
    {
      if (NULL != res)
        {
          priv->resources = g_slist_remove (priv->resources, res);
          _resource_free (res);
          res = NULL;
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

      if (g_strdiff (res->status_message, status_message))
        {
          g_free (res->status_message);
          res->status_message = g_strdup (status_message);
        }

      res->priority = priority;
    }

  /* select the most preferable Resource and update presence->* based on our
   * choice */
  presence->caps = 0;
  presence->status = GABBLE_PRESENCE_OFFLINE;

  /* use the status message from any offline Resource we're
   * keeping around just because it has a message on it */
  presence->status_message = res ? res->status_message : NULL;

  prio = -128;

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      presence->caps |= res->caps;

      /* trump existing status & message if it's more present
       * or has the same presence and a higher priority */
      if (res->status > presence->status ||
          (res->status == presence->status && res->priority > prio))
        {
          presence->status = res->status;
          presence->status_message = res->status_message;
          prio = res->priority;
        }
    }

OUT:
  /* detect changes */
  if (presence->status != old_status ||
      g_strdiff (presence->status_message, old_status_message))
    ret = TRUE;

  g_free (old_status_message);
  return ret;
}

LmMessage *
gabble_presence_as_message (GabblePresence *presence, const gchar *resource)
{
  LmMessage *message;
  LmMessageNode *node;
  LmMessageSubType subtype;
  Resource *res = _find_resource (presence, resource);

  g_assert (NULL != res);

  if (presence->status == GABBLE_PRESENCE_OFFLINE)
    subtype = LM_MESSAGE_SUB_TYPE_UNAVAILABLE;
  else
    subtype = LM_MESSAGE_SUB_TYPE_AVAILABLE;

  message = lm_message_new_with_sub_type (NULL, LM_MESSAGE_TYPE_PRESENCE,
              subtype);
  node = lm_message_get_node (message);

  switch (presence->status)
    {
    case GABBLE_PRESENCE_AVAILABLE:
    case GABBLE_PRESENCE_OFFLINE:
    case GABBLE_PRESENCE_HIDDEN:
      break;
    case GABBLE_PRESENCE_AWAY:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_AWAY);
      break;
    case GABBLE_PRESENCE_CHAT:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_CHAT);
      break;
    case GABBLE_PRESENCE_DND:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_DND);
      break;
    case GABBLE_PRESENCE_XA:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_XA);
      break;
    default:
      g_critical ("%s: Unexpected Telepathy presence type", G_STRFUNC);
      break;
    }

  if (presence->status_message)
      lm_message_node_add_child (node, "status", presence->status_message);

  if (res->priority)
    {
      gchar *priority = g_strdup_printf ("%d", res->priority);
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
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);

  g_string_append_printf (ret,
    "nickname: %s\n"
    "accumulated status: %d\n"
    "accumulated status msg: %s\n"
    "accumulated capabilities: %d\n"
    "kept while unavailable: %d\n"
    "resources:\n", presence->nickname, presence->status,
    presence->status_message, presence->caps,
    presence->keep_unavailable);

  for (i = priv->resources; i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      g_string_append_printf(ret,
        "  %s\n"
        "    capabilities: %d\n"
        "    status: %d\n"
        "    status msg: %s\n"
        "    priority: %d\n", res->name, res->caps, res->status,
        res->status_message, res->priority);
    }

  if (priv->resources == NULL)
    g_string_append_printf(ret, "  (none)\n");

  return g_string_free (ret, FALSE);
}
