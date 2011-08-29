/*
 * room-config.c - Channel.Interface.RoomConfig1 implementation
 * Copyright ©2011 Collabora Ltd.
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

#include "room-config.h"

#include <telepathy-glib/dbus-properties-mixin.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_MUC
#include "debug.h"

/**
 * GabbleRoomConfigClass:
 *
 * Class structure for #GabbleRoomConfig.
 */

/**
 * GabbleRoomConfig:
 *
 * An object representing the configuration of a multi-user chat room.
 *
 * There are no public fields.
 */

struct _GabbleRoomConfigPrivate {
    TpBaseChannel *channel;

    gboolean anonymous;
    gboolean invite_only;
    guint32 limit;
    gboolean moderated;
    gchar *title;
    gchar *description;
    gboolean persistent;
    gboolean private;
    gboolean password_protected;
    gchar *password;
};

enum {
    PROP_CHANNEL = 42,

    /* D-Bus properties */
    PROP_ANONYMOUS,
    PROP_INVITE_ONLY,
    PROP_LIMIT,
    PROP_MODERATED,
    PROP_TITLE,
    PROP_DESCRIPTION,
    PROP_PERSISTENT,
    PROP_PRIVATE,
    PROP_PASSWORD_PROTECTED,
    PROP_PASSWORD,
};

G_DEFINE_TYPE (GabbleRoomConfig, gabble_room_config, G_TYPE_OBJECT)

static void
gabble_room_config_init (GabbleRoomConfig *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GABBLE_TYPE_ROOM_CONFIG,
      GabbleRoomConfigPrivate);
}

static void
gabble_room_config_get_property (
    GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  GabbleRoomConfig *self = GABBLE_ROOM_CONFIG (object);
  GabbleRoomConfigPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CHANNEL:
        g_value_set_object (value, priv->channel);
        break;
      case PROP_ANONYMOUS:
        g_value_set_boolean (value, priv->anonymous);
        break;
      case PROP_INVITE_ONLY:
        g_value_set_boolean (value, priv->invite_only);
        break;
      case PROP_LIMIT:
        g_value_set_uint (value, priv->limit);
        break;
      case PROP_MODERATED:
        g_value_set_boolean (value, priv->moderated);
        break;
      case PROP_TITLE:
        g_value_set_string (value, priv->title);
        break;
      case PROP_DESCRIPTION:
        g_value_set_string (value, priv->description);
        break;
      case PROP_PERSISTENT:
        g_value_set_boolean (value, priv->persistent);
        break;
      case PROP_PRIVATE:
        g_value_set_boolean (value, priv->private);
        break;
      case PROP_PASSWORD_PROTECTED:
        g_value_set_boolean (value, priv->password_protected);
        break;
      case PROP_PASSWORD:
        g_value_set_string (value, priv->password);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
channel_died_cb (
    gpointer data,
    GObject *deceased_channel)
{
  GabbleRoomConfig *self = GABBLE_ROOM_CONFIG (data);
  GabbleRoomConfigPrivate *priv = self->priv;

  DEBUG ("(TpBaseChannel *)%p associated with (GabbleRoomConfig *)%p died",
      deceased_channel, self);
  priv->channel = NULL;
}

static void
gabble_room_config_set_property (
    GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleRoomConfig *self = GABBLE_ROOM_CONFIG (object);
  GabbleRoomConfigPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CHANNEL:
        g_assert (priv->channel == NULL);
        priv->channel = g_value_get_object (value);
        g_assert (priv->channel != NULL);
        g_object_weak_ref (G_OBJECT (priv->channel), channel_died_cb, self);
        DEBUG ("associated (TpBaseChannel *)%p with (GabbleRoomConfig *)%p",
            priv->channel, self);
        break;

      case PROP_ANONYMOUS:
        priv->anonymous = g_value_get_boolean (value);
        break;
      case PROP_INVITE_ONLY:
        priv->invite_only = g_value_get_boolean (value);
        break;
      case PROP_LIMIT:
        priv->limit = g_value_get_uint (value);
        break;
      case PROP_MODERATED:
        priv->moderated = g_value_get_boolean (value);
        break;
      case PROP_TITLE:
        g_free (priv->title);
        priv->title = g_value_dup_string (value);
        break;
      case PROP_DESCRIPTION:
        g_free (priv->description);
        priv->description = g_value_dup_string (value);
        break;
      case PROP_PERSISTENT:
        priv->persistent = g_value_get_boolean (value);
        break;
      case PROP_PRIVATE:
        priv->private = g_value_get_boolean (value);
        break;
      case PROP_PASSWORD_PROTECTED:
        priv->password_protected = g_value_get_boolean (value);
        break;
      case PROP_PASSWORD:
        g_free (priv->password);
        priv->password = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

/* This quark is used to attach a pointer to this object to its parent
 * TpBaseChannel, so we can recover ourself in D-Bus method invocations
 * and property lookups.
 */
static GQuark find_myself_q = 0;

static GabbleRoomConfig *
find_myself (GObject *parent)
{
  GabbleRoomConfig *self = g_object_get_qdata (parent, find_myself_q);

  g_return_val_if_fail (TP_IS_BASE_CHANNEL (parent), NULL);
  g_return_val_if_fail (GABBLE_IS_ROOM_CONFIG (self), NULL);

  return self;
}

static void
gabble_room_config_constructed (GObject *object)
{
  GabbleRoomConfig *self = GABBLE_ROOM_CONFIG (object);
  GabbleRoomConfigPrivate *priv = self->priv;
  GObjectClass *parent_class = gabble_room_config_parent_class;

  if (parent_class->constructed != NULL)
    parent_class->constructed (object);

  g_assert (priv->channel != NULL);
  g_assert (find_myself_q != 0);
  g_object_set_qdata (G_OBJECT (priv->channel), find_myself_q, self);
}

static void
gabble_room_config_dispose (GObject *object)
{
  GabbleRoomConfig *self = GABBLE_ROOM_CONFIG (object);
  GObjectClass *parent_class = gabble_room_config_parent_class;
  GabbleRoomConfigPrivate *priv = self->priv;

  if (priv->channel != NULL)
    {
      g_object_weak_unref (G_OBJECT (priv->channel), channel_died_cb, self);
      priv->channel = NULL;
    }

  if (parent_class->dispose != NULL)
    parent_class->dispose (object);
}

static void
gabble_room_config_finalize (GObject *object)
{
  GabbleRoomConfig *self = GABBLE_ROOM_CONFIG (object);
  GObjectClass *parent_class = gabble_room_config_parent_class;
  GabbleRoomConfigPrivate *priv = self->priv;

  g_free (priv->title);
  g_free (priv->description);
  g_free (priv->password);

  if (parent_class->finalize != NULL)
    parent_class->finalize (object);
}

static void
gabble_room_config_class_init (GabbleRoomConfigClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  object_class->get_property = gabble_room_config_get_property;
  object_class->set_property = gabble_room_config_set_property;
  object_class->constructed = gabble_room_config_constructed;
  object_class->dispose = gabble_room_config_dispose;
  object_class->finalize = gabble_room_config_finalize;

  g_type_class_add_private (klass, sizeof (GabbleRoomConfigPrivate));
  find_myself_q = g_quark_from_static_string ("GabbleRoomConfig pointer");

  param_spec = g_param_spec_object ("channel", "Channel",
      "Parent TpBaseChannel",
      TP_TYPE_BASE_CHANNEL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CHANNEL, param_spec);

  /* D-Bus properties. */
  param_spec = g_param_spec_boolean ("anonymous", "Anonymous",
      "True if people may join the channel without other members being made "
      "aware of their identity.",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ANONYMOUS, param_spec);

  param_spec = g_param_spec_boolean ("invite-only", "InviteOnly",
      "True if people may not join the channel until they have been invited.",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INVITE_ONLY, param_spec);

  param_spec = g_param_spec_uint ("limit", "Limit",
      "The limit to the number of members; or 0 if there is no limit.",
      0, G_MAXUINT32, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LIMIT, param_spec);

  param_spec = g_param_spec_boolean ("moderated", "Moderated",
      "True if channel membership is not sufficient to allow participation.",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MODERATED, param_spec);

  param_spec = g_param_spec_string ("title", "Title",
      "A human-visible name for the channel, if it differs from "
      "Room.DRAFT.RoomName; the empty string, otherwise.",
      "",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TITLE, param_spec);

  param_spec = g_param_spec_string ("description", "Description",
      "A human-readable description of the channel's overall purpose; if any.",
      "",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DESCRIPTION, param_spec);

  param_spec = g_param_spec_boolean ("persistent", "Persistent",
      "True if the channel will remain in existence on the server after all "
      "members have left it.",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PERSISTENT, param_spec);

  param_spec = g_param_spec_boolean ("private", "Private",
      "True if the channel is not visible to non-members.",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PRIVATE, param_spec);

  param_spec = g_param_spec_boolean ("password-protected", "PasswordProtected",
      "True if contacts joining this channel must provide a password to be "
      "granted entry.",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PASSWORD_PROTECTED,
      param_spec);

  param_spec = g_param_spec_string ("password", "Password",
      "If PasswordProtected is True, the password required to enter the "
      "channel, if known. If the password is unknown, or PasswordProtected "
      "is False, the empty string.",
      "",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PASSWORD, param_spec);
}

/* room_config_getter:
 *
 * This is basically an indirected version of
 * tp_dbus_properties_mixin_getter_gobject_properties to cope with this GObject
 * not actually being the exported D-Bus object.
 */
static void
room_config_getter (
    GObject *object,
    GQuark iface,
    GQuark name,
    GValue *value,
    gpointer getter_data)
{
  GabbleRoomConfig *self = find_myself (object);

  g_return_if_fail (self != NULL);

  g_object_get_property ((GObject *) self, getter_data, value);
}

static TpDBusPropertiesMixinPropImpl room_config_properties[] = {
  { "Anonymous", "anonymous", NULL, },
  { "InviteOnly", "invite-only", NULL },
  { "Limit", "limit", NULL },
  { "Moderated", "moderated", NULL },
  { "Title", "title", NULL },
  { "Description", "description", NULL },
  { "Persistent", "persistent", NULL },
  { "Private", "private", NULL },
  { "PasswordProtected", "password-protected", NULL },
  { "Password", "password", NULL },
  { NULL }
};

/**
 * gabble_room_config_register_class:
 * @base_channel_class: the class structure for a subclass of #TpBaseChannel
 *  which uses this object to implement #TP_SVC_CHANNEL_INTERFACE_ROOM_CONFIG
 *
 * Registers that D-Bus properties for the RoomConfig1 interface should be
 * handled by a #GabbleRoomConfig object associated with instances of
 * @base_channel_class.
 *
 * @base_channel_class must implement #TP_SVC_CHANNEL_INTERFACE_ROOM_CONFIG
 * using gabble_room_config_iface_init(), and instances of @base_channel_class
 * must construct an instance of #GabbleRoomConfig, passing themself as
 * #GabbleRoomConfig:channel.
 */
void
gabble_room_config_register_class (
    TpBaseChannelClass *base_channel_class)
{
  GObjectClass *cls = G_OBJECT_CLASS (base_channel_class);

  tp_dbus_properties_mixin_implement_interface (cls,
      TP_IFACE_QUARK_CHANNEL_INTERFACE_ROOM_CONFIG,
      room_config_getter, NULL, room_config_properties);
}

/**
 * gabble_room_config_iface_init:
 * @g_iface: a pointer to a #TpSvcChannelInterfaceRoomConfigClass structure
 * @iface_data: ignored
 *
 * Pass this as the second argument to G_IMPLEMENT_INTERFACE() when defining a
 * #TpBaseChannel subclass to declare that TP_SVC_CHANNEL_INTERFACE_ROOM_CONFIG
 * is implemented using this class. The #TpBaseChannel subclass must also call
 * gabble_room_config_register_class() in its class_init function, and
 * construct a #GabbleRoomConfig object for each instance.
 */
void
gabble_room_config_iface_init (
    gpointer g_iface,
    gpointer iface_data)
{
#define IMPLEMENT(x) tp_svc_channel_interface_room_config_implement_##x (\
    g_iface, gabble_room_config_##x)
/*  IMPLEMENT (update_configuration); */
#undef IMPLEMENT
}

GabbleRoomConfig *
gabble_room_config_new (
    TpBaseChannel *channel)
{
  g_return_val_if_fail (TP_IS_BASE_CHANNEL (channel), NULL);

  return g_object_new (GABBLE_TYPE_ROOM_CONFIG,
      "channel", channel,
      NULL);
}
