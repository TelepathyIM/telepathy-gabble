/*
 * wocky-pep-service.c - WockyPepService
 * Copyright (C) 2009 Collabora Ltd.
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

#include "wocky-pep-service.h"

#include <wocky/wocky-porter.h>
#include <wocky/wocky-utils.h>

G_DEFINE_TYPE (WockyPepService, wocky_pep_service, G_TYPE_OBJECT)

/* signal enum */
enum
{
  LAST_SIGNAL,
};

/*
static guint signals[LAST_SIGNAL] = {0};
*/

enum
{
  PROP_NODE = 1,
  PROP_SUBSCRIBE,
};

/* private structure */
typedef struct _WockyPepServicePrivate WockyPepServicePrivate;

struct _WockyPepServicePrivate
{
  WockySession *session;
  WockyPorter *porter;

  gchar *node;
  gboolean subscribe;

  gboolean dispose_has_run;
};

#define WOCKY_PEP_SERVICE_GET_PRIVATE(o)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), WOCKY_TYPE_PEP_SERVICE, \
    WockyPepServicePrivate))

static void
wocky_pep_service_init (WockyPepService *obj)
{
  /*
  WockyPepService *self = WOCKY_PEP_SERVICE (obj);
  WockyPepServicePrivate *priv = WOCKY_PEP_SERVICE_GET_PRIVATE (self);
  */
}

static void
wocky_pep_service_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  WockyPepService *self = WOCKY_PEP_SERVICE (object);
  WockyPepServicePrivate *priv = WOCKY_PEP_SERVICE_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_NODE:
        priv->node = g_value_dup_string (value);
        break;
      case PROP_SUBSCRIBE:
        priv->subscribe = g_value_get_boolean (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
wocky_pep_service_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  WockyPepService *self = WOCKY_PEP_SERVICE (object);
  WockyPepServicePrivate *priv = WOCKY_PEP_SERVICE_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_NODE:
        g_value_set_string (value, priv->node);
        break;
      case PROP_SUBSCRIBE:
        g_value_set_boolean (value, priv->subscribe);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}


static void
wocky_pep_service_dispose (GObject *object)
{
  WockyPepService *self = WOCKY_PEP_SERVICE (object);
  WockyPepServicePrivate *priv = WOCKY_PEP_SERVICE_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->porter != NULL)
    g_object_unref (priv->porter);

  if (G_OBJECT_CLASS (wocky_pep_service_parent_class)->dispose)
    G_OBJECT_CLASS (wocky_pep_service_parent_class)->dispose (object);
}

static void
wocky_pep_service_finalize (GObject *object)
{
  WockyPepService *self = WOCKY_PEP_SERVICE (object);
  WockyPepServicePrivate *priv = WOCKY_PEP_SERVICE_GET_PRIVATE (self);

  g_free (priv->node);

  G_OBJECT_CLASS (wocky_pep_service_parent_class)->finalize (object);
}

static void
wocky_pep_service_constructed (GObject *object)
{
  WockyPepService *self = WOCKY_PEP_SERVICE (object);
  WockyPepServicePrivate *priv = WOCKY_PEP_SERVICE_GET_PRIVATE (self);

  g_assert (priv->node != NULL);
}

static void
wocky_pep_service_class_init (WockyPepServiceClass *wocky_pep_service_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (wocky_pep_service_class);
  GParamSpec *param_spec;

  g_type_class_add_private (wocky_pep_service_class,
      sizeof (WockyPepServicePrivate));

  object_class->set_property = wocky_pep_service_set_property;
  object_class->get_property = wocky_pep_service_get_property;
  object_class->dispose = wocky_pep_service_dispose;
  object_class->finalize = wocky_pep_service_finalize;
  object_class->constructed = wocky_pep_service_constructed;

  param_spec = g_param_spec_string ("node", "node",
      "namespace of the pep node",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NODE, param_spec);

  param_spec = g_param_spec_boolean ("subscribe", "subscribe",
      "if TRUE, Wocky will subscribe to the notifications of the node",
      FALSE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SUBSCRIBE, param_spec);
}

WockyPepService *
wocky_pep_service_new (const gchar *node,
    gboolean subscribe)
{
  return g_object_new (WOCKY_TYPE_PEP_SERVICE,
      "node", node,
      "subscribe", subscribe,
      NULL);
}

void
wocky_pep_service_start (WockyPepService *self,
    WockySession *session)
{
  WockyPepServicePrivate *priv = WOCKY_PEP_SERVICE_GET_PRIVATE (self);

  g_assert (priv->session == NULL);
  priv->session = session;

  priv->porter = wocky_session_get_porter (priv->session);
  g_object_ref (priv->porter);
}
