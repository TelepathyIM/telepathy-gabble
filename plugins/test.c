#include "test.h"

#include <stdio.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/errors.h>

#include "extensions/extensions.h"

#include "plugin.h"

#define DEBUG(msg, ...) \
  g_debug ("%s: " msg, G_STRFUNC, ##__VA_ARGS__)

/*****************************
 * TestPlugin implementation *
 *****************************/

static void plugin_iface_init (
    gpointer g_iface,
    gpointer data);

#define IFACE_TEST "org.freedesktop.Telepathy.Gabble.Plugin.Test"
#define IFACE_TEST_PROPS IFACE_TEST ".Props"
#define IFACE_TEST_BUGGY IFACE_TEST ".Buggy"

static const gchar * const sidecar_interfaces[] = {
    IFACE_TEST,
    IFACE_TEST_PROPS,
    IFACE_TEST_BUGGY,
    NULL
};

G_DEFINE_TYPE_WITH_CODE (TestPlugin, test_plugin, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_PLUGIN, plugin_iface_init);
    )

static void
test_plugin_init (TestPlugin *object)
{
  DEBUG ("%p", object);
}

static void
test_plugin_class_init (TestPluginClass *klass)
{
}

static void
test_plugin_create_sidecar (
    GabblePlugin *plugin,
    const gchar *sidecar_interface,
    TpBaseConnection *connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (plugin),
      callback, user_data,
      /* sic: all plugins share gabble_plugin_create_sidecar_finish() so we
       * need to use the same source tag.
       */
      gabble_plugin_create_sidecar);
  GabbleSidecar *sidecar = NULL;

  if (!tp_strdiff (sidecar_interface, IFACE_TEST))
    {
      sidecar = g_object_new (TEST_TYPE_SIDECAR, NULL);
    }
  else if (!tp_strdiff (sidecar_interface, IFACE_TEST_PROPS))
    {
      sidecar = g_object_new (TEST_TYPE_SIDECAR_PROPS, NULL);
    }
  else
    {
      /* This deliberately doesn't check for IFACE_TEST_BUGGY, to test Gabble's
       * reactions to buggy plugins. :)
       */
      g_simple_async_result_set_error (result, TP_ERRORS,
          TP_ERROR_NOT_IMPLEMENTED, "'%s' not implemented", sidecar_interface);
    }

  if (sidecar != NULL)
    g_simple_async_result_set_op_res_gpointer (result, sidecar, g_object_unref);

  g_simple_async_result_complete_in_idle (result);
  g_object_unref (result);
}

static void
plugin_iface_init (
    gpointer g_iface,
    gpointer data G_GNUC_UNUSED)
{
  GabblePluginInterface *iface = g_iface;

  iface->name = "Sidecar test plugin";
  iface->sidecar_interfaces = sidecar_interfaces;
  iface->create_sidecar = test_plugin_create_sidecar;
}

GabblePlugin *
gabble_plugin_create ()
{
  return g_object_new (test_plugin_get_type (), NULL);
}

/******************************
 * TestSidecar implementation *
 ******************************/

static void sidecar_iface_init (
    gpointer g_iface,
    gpointer data);

G_DEFINE_TYPE_WITH_CODE (TestSidecar, test_sidecar, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SIDECAR, sidecar_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_GABBLE_PLUGIN_TEST, NULL);
    )

static void
test_sidecar_init (TestSidecar *object)
{
  DEBUG ("%p", object);
}

static void
test_sidecar_class_init (TestSidecarClass *klass)
{
}

static void sidecar_iface_init (
    gpointer g_iface,
    gpointer data)
{
  GabbleSidecarInterface *iface = g_iface;

  iface->interface = IFACE_TEST;
  iface->get_immutable_properties = NULL;
}

/***********************************
 * TestSidecarProps implementation *
 ***********************************/

static void sidecar_props_iface_init (
    gpointer g_iface,
    gpointer data);

G_DEFINE_TYPE_WITH_CODE (TestSidecarProps, test_sidecar_props, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SIDECAR, sidecar_props_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_GABBLE_PLUGIN_TEST, NULL);
    )

static void
test_sidecar_props_init (TestSidecarProps *object)
{
  DEBUG ("%p", object);

  object->props = tp_asv_new (
      IFACE_TEST_PROPS ".Greeting", G_TYPE_STRING, "oh hai",
      NULL);
}

static void
test_sidecar_props_finalize (GObject *object)
{
  TestSidecarProps *self = TEST_SIDECAR_PROPS (object);
  void (*chain_up) (GObject *) =
      G_OBJECT_CLASS (test_sidecar_props_parent_class)->finalize;

  g_hash_table_unref (self->props);

  if (chain_up != NULL)
    chain_up (object);
}

static void
test_sidecar_props_class_init (TestSidecarPropsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = test_sidecar_props_finalize;
}

static GHashTable *
sidecar_props_get_immutable_properties (GabbleSidecar *sidecar)
{
  TestSidecarProps *self = TEST_SIDECAR_PROPS (sidecar);

  return g_hash_table_ref (self->props);
}

static void sidecar_props_iface_init (
    gpointer g_iface,
    gpointer data)
{
  GabbleSidecarInterface *iface = g_iface;

  iface->interface = IFACE_TEST_PROPS;
  iface->get_immutable_properties = sidecar_props_get_immutable_properties;
}



