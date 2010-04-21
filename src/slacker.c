/*
 * slacker.c - Maemo device state monitor
 * Copyright ©2010 Collabora Ltd.
 * Copyright ©2008-2010 Nokia Corporation
 *
 * Derived from code in e-book-backend-tp.c in eds-backend-telepathy; thanks!
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

#include "slacker.h"
#include "config.h"

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#ifdef HAVE_MCE
#include <mce/dbus-names.h>
#else /* HAVE_MCE */

/* Use some dummy interfaces etc. for the test suite.
 *
 * In a perfect world of sweetness and light these would not be enabled for the
 * real build, but we do not live in a perfect world of sweetness and light: we
 * live below a dark cloud of bitter ash, the charred remains of a defunct
 * economy and cygnine clothing.
 */
#define MCE_SERVICE "org.freedesktop.Telepathy.Gabble.Tests.MCE"

#define MCE_SIGNAL_IF "org.freedesktop.Telepathy.Gabble.Tests.MCE"
#define MCE_INACTIVITY_SIG "InactivityChanged"

#define MCE_REQUEST_IF "org.freedesktop.Telepathy.Gabble.Tests.MCE"
#define MCE_REQUEST_PATH "/org/freedesktop/Telepathy/Gabble/Tests/MCE"
#define MCE_INACTIVITY_STATUS_GET "GetInactivity"

#endif /* HAVE_MCE */

#define DEBUG_FLAG GABBLE_DEBUG_SLACKER
#include "debug.h"
#include "gabble-signals-marshal.h"

struct _GabbleSlackerPrivate {
    DBusGConnection *bus;
    DBusGProxy *mce_request_proxy;

    gboolean is_inactive;
};

G_DEFINE_TYPE (GabbleSlacker, gabble_slacker, G_TYPE_OBJECT)

enum {
    SIG_INACTIVITY_CHANGED = 0,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/**
 * gabble_slacker_is_inactive:
 * @self: do some work!
 *
 * <!-- -->
 *
 * Returns: %TRUE if the device is known to be inactive; false otherwise.
 */
gboolean
gabble_slacker_is_inactive (GabbleSlacker *self)
{
  g_return_val_if_fail (GABBLE_IS_SLACKER (self), FALSE);

  return self->priv->is_inactive;
}

static void
slacker_inactivity_changed (
    GabbleSlacker *self,
    gboolean is_inactive)
{
  GabbleSlackerPrivate *priv = self->priv;
  gboolean old = priv->is_inactive;

  priv->is_inactive = is_inactive;

  if (!!old != !!is_inactive)
    {
      DEBUG ("device became %s", (is_inactive ? "inactive" : "active"));
      g_signal_emit (self, signals[SIG_INACTIVITY_CHANGED], 0, is_inactive);
    }
}

static GQuark mce_signal_interface_quark = 0;
static GQuark mce_inactivity_signal_quark = 0;

#define INACTIVITY_MATCH_RULE \
  "type='signal',interface='" MCE_SIGNAL_IF "',member='" MCE_INACTIVITY_SIG "'"

static DBusHandlerResult
slacker_message_filter (
    DBusConnection *connection,
    DBusMessage *message,
    gpointer user_data)
{
  GabbleSlacker *self = GABBLE_SLACKER (user_data);
  GQuark interface, member;
  int message_type;

  interface = g_quark_try_string (dbus_message_get_interface (message));
  member = g_quark_try_string (dbus_message_get_member (message));
  message_type = dbus_message_get_type (message);

  if (interface == mce_signal_interface_quark &&
      message_type == DBUS_MESSAGE_TYPE_SIGNAL &&
      member == mce_inactivity_signal_quark)
    {
      gboolean is_inactive;

      dbus_message_get_args (message, NULL, DBUS_TYPE_BOOLEAN, &is_inactive,
          DBUS_TYPE_INVALID);
      slacker_inactivity_changed (self, is_inactive);
    }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
get_inactivity_status_cb (
    DBusGProxy *proxy,
    DBusGProxyCall *call,
    gpointer user_data)
{
  GabbleSlacker *self = GABBLE_SLACKER (user_data);
  gboolean is_inactive;

  if (dbus_g_proxy_end_call (proxy, call, NULL /* ignore errors */,
          G_TYPE_BOOLEAN, &is_inactive,
          G_TYPE_INVALID))
    slacker_inactivity_changed (self, is_inactive);

  g_object_unref (self->priv->mce_request_proxy);
  self->priv->mce_request_proxy = NULL;
}

static void
slacker_add_filter (GabbleSlacker *self)
{
  GabbleSlackerPrivate *priv = self->priv;
  DBusConnection *c = dbus_g_connection_get_connection (self->priv->bus);

  dbus_connection_add_filter (c, slacker_message_filter, self, NULL);
  dbus_bus_add_match (c, INACTIVITY_MATCH_RULE, NULL);

  priv->mce_request_proxy = dbus_g_proxy_new_for_name (priv->bus,
      MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF);
  dbus_g_proxy_begin_call (priv->mce_request_proxy, MCE_INACTIVITY_STATUS_GET,
      get_inactivity_status_cb, self, NULL, G_TYPE_INVALID);
}

static void
slacker_remove_filter (GabbleSlacker *self)
{
  DBusConnection *c = dbus_g_connection_get_connection (self->priv->bus);

  dbus_connection_remove_filter (c, slacker_message_filter, self);
  dbus_bus_remove_match (c, INACTIVITY_MATCH_RULE, NULL);
}

/* GObject boilerplate */

static void
gabble_slacker_init (GabbleSlacker *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GABBLE_TYPE_SLACKER,
      GabbleSlackerPrivate);
}

static gpointer slacker = NULL;

static GObject *
gabble_slacker_constructor (
    GType type,
    guint n_construct_properties,
    GObjectConstructParam *construct_properties)
{
  GObject *retval;

  if (slacker == NULL)
    {
      slacker = G_OBJECT_CLASS (gabble_slacker_parent_class)->constructor (
        type, n_construct_properties, construct_properties);
      retval = slacker;
      g_object_add_weak_pointer (retval, &slacker);
    }
  else
    {
      retval = g_object_ref (slacker);
    }

  return retval;
}

static void
gabble_slacker_constructed (GObject *object)
{
  GabbleSlacker *self = GABBLE_SLACKER (object);
  GError *error = NULL;

#ifdef HAVE_MCE
  self->priv->bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
#else
  self->priv->bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
#endif

  if (self->priv->bus == NULL)
    {
      g_warning ("help! where did my system bus go? %s", error->message);
      g_clear_error (&error);
    }
  else
    {
      slacker_add_filter (self);
    }
}

static void
gabble_slacker_dispose (GObject *object)
{
  GabbleSlacker *self = GABBLE_SLACKER (object);
  GabbleSlackerPrivate *priv = self->priv;

  if (priv->mce_request_proxy != NULL)
    {
      g_object_unref (priv->mce_request_proxy); /* this cancels pending calls */
      priv->mce_request_proxy = NULL;
    }

  if (priv->bus != NULL)
    {
      slacker_remove_filter (self);
      dbus_g_connection_unref (priv->bus);
      priv->bus = NULL;
    }
}

static void
gabble_slacker_finalize (GObject *object)
{
  /* GabbleSlacker *self = GABBLE_SLACKER (object); */
}

static void
gabble_slacker_class_init (GabbleSlackerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructor = gabble_slacker_constructor;
  object_class->constructed = gabble_slacker_constructed;
  object_class->dispose = gabble_slacker_dispose;
  object_class->finalize = gabble_slacker_finalize;

  g_type_class_add_private (klass, sizeof (GabbleSlackerPrivate));

  /**
   * GabbleSlacker::inactivity-changed:
   * @self: what a slacker
   * @inactive: %TRUE if the device is inactive.
   *
   * The ::inactivity-changed is emitted whenever MCE declares that the device
   * has become active or inactive. Note that there is a lag (of around 30
   * seconds, at the time of writing) between the screen blanking and MCE
   * declaring the device inactive.
   */
  signals[SIG_INACTIVITY_CHANGED] = g_signal_new ("inactivity-changed",
      GABBLE_TYPE_SLACKER, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      gabble_marshal_VOID__BOOLEAN,
      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

  if (!mce_signal_interface_quark)
    {
      mce_signal_interface_quark = g_quark_from_static_string (MCE_SIGNAL_IF);
      mce_inactivity_signal_quark = g_quark_from_static_string (
          MCE_INACTIVITY_SIG);
    }
}

GabbleSlacker *
gabble_slacker_new ()
{
  return g_object_new (GABBLE_TYPE_SLACKER, NULL);
}
