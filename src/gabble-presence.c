
#include <string.h>

#include <glib.h>

#include "gabble-presence.h"

G_DEFINE_TYPE (GabblePresence, gabble_presence, G_TYPE_OBJECT);

#define GABBLE_PRESENCE_PRIV(account) ((GabblePresencePrivate *)account->priv)

typedef struct _Resource Resource;

struct _Resource {
    gchar *name;
    GabblePresenceCapabilities caps;
    GabblePresenceId status;
    gchar *status_message;
    gchar *status_name;
    gint8 priority;
};

typedef struct _GabblePresencePrivate GabblePresencePrivate;

struct _GabblePresencePrivate {
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
  if (resource->status_message)
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

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      if (res->caps & caps)
        return res->name;
    }

  return NULL;
}

void
gabble_presence_set_capabilities (GabblePresence *presence, const gchar *resource, GabblePresenceCapabilities caps)
{
  GSList *i;
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);

  if (caps & PRESENCE_CAP_GOOGLE_VOICE)
    g_debug ("setting Google voice cap for resource %s", resource);

  if (caps & PRESENCE_CAP_JINGLE_VOICE)
    g_debug ("setting Jingle voice cap for resource %s", resource);

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *tmp = (Resource *) i->data;

      if (0 == strcmp (tmp->name, resource))
        {
          tmp->caps |= caps;
          break;
        }
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

void
gabble_presence_update (GabblePresence *presence, const gchar *resource, GabblePresenceId status, const gchar *status_message, gint8 priority)
{
  GabblePresencePrivate *priv = GABBLE_PRESENCE_PRIV (presence);
  GSList *i;

  g_assert (NULL != resource);
  g_debug ("presence update: %s/%d/%s/%d", resource, status, status_message, priority);

  if (status == GABBLE_PRESENCE_OFFLINE)
    {
      Resource *res = _find_resource (presence, resource);

      if (NULL == res)
        return;

      priv->resources = g_slist_remove (priv->resources, res);

      presence->status = GABBLE_PRESENCE_OFFLINE;
      presence->status_message = NULL;
    }
  else
    {
      Resource *res = _find_resource (presence, resource);

      if (NULL == res)
        {
          res = _resource_new (g_strdup (resource));
          priv->resources = g_slist_append (priv->resources, res);
        }

      res->status = status;
      g_free (res->status_message);
      res->status_message = g_strdup (status_message);
      res->priority = priority;

      presence->status = res->status;
      presence->status_message = res->status_message;
    }

  for (i = priv->resources; NULL != i; i = i->next)
    {
      Resource *res = (Resource *) i->data;

      if (res->status < presence->status)
        {
          presence->status = res->status;
          presence->status_message = res->status_message;
        }
    }
}

