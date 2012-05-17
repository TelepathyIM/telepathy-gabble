#include "config.h"

#include "test.h"

#include <stdio.h>

#include <telepathy-glib/telepathy-glib.h>

#include <wocky/wocky.h>

#include "extensions/extensions.h"

#include <gabble/plugin.h>
#include <gabble/caps-channel-manager.h>

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
#define IFACE_TEST_IQ IFACE_TEST ".IQ"

static const gchar * const sidecar_interfaces[] = {
    IFACE_TEST,
    IFACE_TEST_PROPS,
    IFACE_TEST_BUGGY,
    IFACE_TEST_IQ,
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
sidecar_iq_created_cb (
    GObject *source,
    GAsyncResult *new_result,
    gpointer user_data)
{
  GSimpleAsyncResult *result = user_data;
  GabbleSidecar *sidecar = GABBLE_SIDECAR (source);
  GError *error = NULL;

  if (g_async_initable_init_finish (G_ASYNC_INITABLE (source),
          new_result, &error))
    {
      g_simple_async_result_set_op_res_gpointer (result, sidecar,
          g_object_unref);
    }
  else
    {
      g_simple_async_result_set_from_error (result, error);
      g_clear_error (&error);
      g_object_unref (sidecar);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

static void
test_plugin_create_sidecar_async (
    GabblePlugin *plugin,
    const gchar *sidecar_interface,
    GabblePluginConnection *plugin_connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (plugin),
      callback, user_data,
      test_plugin_create_sidecar_async);

  GabbleSidecar *sidecar = NULL;

  if (!tp_strdiff (sidecar_interface, IFACE_TEST))
    {
      sidecar = g_object_new (TEST_TYPE_SIDECAR, NULL);
    }
  else if (!tp_strdiff (sidecar_interface, IFACE_TEST_PROPS))
    {
      sidecar = g_object_new (TEST_TYPE_SIDECAR_PROPS, NULL);
    }
  else if (!tp_strdiff (sidecar_interface, IFACE_TEST_IQ))
    {
      g_async_initable_new_async (TEST_TYPE_SIDECAR_IQ, G_PRIORITY_DEFAULT,
          NULL, sidecar_iq_created_cb, result, "session", session,
          "plugin-connection", plugin_connection, NULL);
      return;
    }
  else
    {
      /* This deliberately doesn't check for IFACE_TEST_BUGGY, to test Gabble's
       * reactions to buggy plugins. :)
       */
      g_simple_async_result_set_error (result, TP_ERROR,
          TP_ERROR_NOT_IMPLEMENTED, "'%s' not implemented", sidecar_interface);
    }

  if (sidecar != NULL)
    g_simple_async_result_set_op_res_gpointer (result, sidecar, g_object_unref);

  g_simple_async_result_complete_in_idle (result);

  g_object_unref (result);
}

static GabbleSidecar *
test_plugin_create_sidecar_finish (
    GabblePlugin *plugin,
    GAsyncResult *result,
    GError **error)
{
  GabbleSidecar *sidecar;

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
        error))
    return NULL;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
        G_OBJECT (plugin), test_plugin_create_sidecar_async), NULL);

  sidecar = GABBLE_SIDECAR (g_simple_async_result_get_op_res_gpointer (
        G_SIMPLE_ASYNC_RESULT (result)));

  return g_object_ref (sidecar);
}

static GPtrArray *
test_plugin_create_channel_managers (GabblePlugin *plugin,
    GabblePluginConnection *plugin_connection)
{
  GPtrArray *ret = g_ptr_array_new ();

  DEBUG ("plugin %p on connection %p", plugin, plugin_connection);

  g_ptr_array_add (ret,
      g_object_new (TEST_TYPE_CHANNEL_MANAGER,
          "plugin-connection", plugin_connection,
          NULL));

  return ret;
}

static TpPresenceStatusSpec test_presences[] = {
  { "testbusy", TP_CONNECTION_PRESENCE_TYPE_BUSY, TRUE, NULL, NULL, NULL },
  { "testaway", TP_CONNECTION_PRESENCE_TYPE_AWAY, FALSE, NULL, NULL, NULL },
  { NULL, 0, FALSE, NULL, NULL, NULL }
};

static GabblePluginPrivacyListMap privacy_list_map[] = {
  { "testbusy", "test-busy-list" },
  { NULL, NULL },
};

static void
plugin_iface_init (
    gpointer g_iface,
    gpointer data G_GNUC_UNUSED)
{
  GabblePluginInterface *iface = g_iface;

  iface->name = "Sidecar test plugin";
  iface->version = PACKAGE_VERSION;
  iface->sidecar_interfaces = sidecar_interfaces;
  iface->create_sidecar_async = test_plugin_create_sidecar_async;
  iface->create_sidecar_finish = test_plugin_create_sidecar_finish;
  iface->create_channel_managers = test_plugin_create_channel_managers;

  iface->presence_statuses = test_presences;
  iface->privacy_list_map = privacy_list_map;
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

/********************************
 * TestSidecarIQ implementation *
 ********************************/

static void sidecar_iq_iface_init (
    gpointer g_iface,
    gpointer data);
static void async_initable_iface_init (
    gpointer g_iface,
    gpointer data);

G_DEFINE_TYPE_WITH_CODE (TestSidecarIQ, test_sidecar_iq, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SIDECAR, sidecar_iq_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_GABBLE_PLUGIN_TEST, NULL);
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init);
    )

enum {
    PROP_SESSION = 1,
    PROP_CONNECTION = 2
};

static void
test_sidecar_iq_init (TestSidecarIQ *object)
{
  DEBUG ("%p", object);
}

static void
test_sidecar_iq_set_property (
    GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  TestSidecarIQ *self = TEST_SIDECAR_IQ (object);

  switch (property_id)
    {
      case PROP_SESSION:
        self->session = g_value_dup_object (value);
        break;
      case PROP_CONNECTION:
        {
          self->connection = g_value_dup_object (value);

          if (self->connection)
            {
              GabbleCapabilitySet *features;
              const gchar *applications[] = { "com.example.test1",
                  "com.example.test2", NULL };
              GPtrArray *identities;
              WockyDiscoIdentity *identity;
              gchar *hash;
              guint i;

              features = gabble_capability_set_new ();
              for (i = 0; applications[i] != NULL; i++)
                gabble_capability_set_add (features, applications[i]);

              identities = wocky_disco_identity_array_new ();
              identity = wocky_disco_identity_new ("test", "app-list",
                  NULL, "Test");
              g_ptr_array_add (identities, identity);

              /* set own caps so we proper reply to disco#info */
              hash = gabble_plugin_connection_add_sidecar_own_caps (self->connection,
                  features, identities);

              g_free (hash);
              wocky_disco_identity_array_free (identities);
              gabble_capability_set_free (features);
            }
        }
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void
test_sidecar_iq_dispose (GObject *object)
{
  TestSidecarIQ *self = TEST_SIDECAR_IQ (object);

  DEBUG ("called for %p", object);

  tp_clear_object (&self->session);
  tp_clear_object (&self->connection);
}

static void
test_sidecar_iq_class_init (TestSidecarIQClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = test_sidecar_iq_set_property;
  object_class->dispose = test_sidecar_iq_dispose;

  g_object_class_install_property (object_class, PROP_SESSION,
      g_param_spec_object ("session", "SESSION",
          "THIS IS A WOCKY SESSION YOU CAN TELL BY THE TYPE",
          WOCKY_TYPE_SESSION,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_CONNECTION,
      g_param_spec_object ("plugin-connection", "Gabble Plugin Connection",
          "Gabble Plugin Connection",
          GABBLE_TYPE_PLUGIN_CONNECTION,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
sidecar_iq_iface_init (
    gpointer g_iface,
    gpointer data)
{
  GabbleSidecarInterface *iface = g_iface;

  iface->interface = IFACE_TEST_IQ;
  iface->get_immutable_properties = NULL;
}

static void
iq_cb (
    GObject *source,
    GAsyncResult *nested_result,
    gpointer user_data)
{
  GSimpleAsyncResult *result = user_data;
  GError *error = NULL;
  WockyStanza *reply;

  reply = wocky_porter_send_iq_finish (WOCKY_PORTER (source),
      nested_result, &error);

  if (reply == NULL)
    {
      g_simple_async_result_set_from_error (result, error);
      g_clear_error (&error);
    }
  else
    {
      WockyStanzaSubType t;

      wocky_stanza_get_type_info (reply, NULL, &t);

      if (t == WOCKY_STANZA_SUB_TYPE_RESULT)
        g_simple_async_result_set_op_res_gboolean (result, TRUE);
      else
        g_simple_async_result_set_error (result, TP_ERROR,
            TP_ERROR_NOT_AVAILABLE, "server said no!");

      g_object_unref (reply);
    }

  g_simple_async_result_complete (result);
}

static void
sidecar_iq_init_async (
    GAsyncInitable *initable,
    int io_priority,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  TestSidecarIQ *self = TEST_SIDECAR_IQ (initable);
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, sidecar_iq_init_async);
  WockyPorter *porter = wocky_session_get_porter (self->session);
  WockyStanza *iq;

  iq = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_GET, NULL, "sidecar.example.com",
        '(', "query",
          ':', "http://example.com/sidecar",
          '(', "oh-hai",
          ')',
        ')',
      NULL);
  wocky_porter_send_iq_async (porter, iq, cancellable, iq_cb, result);
}

static gboolean
sidecar_iq_init_finish (
    GAsyncInitable *initable,
    GAsyncResult *result,
    GError **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
      G_OBJECT (initable), sidecar_iq_init_async), FALSE);

  return g_simple_async_result_get_op_res_gboolean (simple);
}

static void
async_initable_iface_init (
    gpointer g_iface,
    gpointer data)
{
  GAsyncInitableIface *iface = g_iface;

  iface->init_async = sidecar_iq_init_async;
  iface->init_finish = sidecar_iq_init_finish;
}

/***********************************
 * TestChannelManager implementation *
 ***********************************/
static void channel_manager_iface_init (gpointer, gpointer);
static void caps_channel_manager_iface_init (gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (TestChannelManager, test_channel_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
        channel_manager_iface_init)
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER,
        caps_channel_manager_iface_init));

static void
test_channel_manager_init (TestChannelManager *self)
{
}

static void
test_channel_manager_set_property (
    GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  TestChannelManager *self = TEST_CHANNEL_MANAGER (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        /* Not reffing this: the connection owns all channel managers, so it
         * must outlive us. Taking a reference leads to a cycle.
         */
        self->plugin_connection = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
test_channel_manager_get_property (
    GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  TestChannelManager *self = TEST_CHANNEL_MANAGER (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, self->plugin_connection);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
test_channel_manager_porter_available_cb (GabbleConnection *connection,
    WockyPorter *porter,
    gpointer user_data)
{
  DEBUG ("now we have a porter: %p", porter);
  /* so now we can call things like wocky_porter_register_handler_*
   * and get some stanzas. */
}

static void
test_channel_manager_constructed (GObject *object)
{
  TestChannelManager *self = TEST_CHANNEL_MANAGER (object);

  if (G_OBJECT_CLASS (test_channel_manager_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (test_channel_manager_parent_class)->constructed (object);

  tp_g_signal_connect_object (self->plugin_connection,
      "porter-available",
      G_CALLBACK (test_channel_manager_porter_available_cb),
      self, 0);
}

static void
test_channel_manager_class_init (TestChannelManagerClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->set_property = test_channel_manager_set_property;
  oclass->get_property = test_channel_manager_get_property;
  oclass->constructed = test_channel_manager_constructed;

  g_object_class_install_property (oclass, PROP_CONNECTION,
      g_param_spec_object ("plugin-connection", "Gabble Plugin Connection",
          "Gabble Plugin Connection",
          GABBLE_TYPE_PLUGIN_CONNECTION,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
test_channel_manager_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, "com.jonnylamb.lolbags",
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_NONE,
      NULL);
  const gchar * const chock_a_block_full_of_strings[] = {
      "com.jonnylamb.omg", "com.jonnylamb.brokethebuild", NULL };

  func (type, table, chock_a_block_full_of_strings, user_data);

  g_hash_table_unref (table);
}

static void
test_channel_manager_represent_client (
    GabbleCapsChannelManager *manager,
    const gchar *client_name,
    const GPtrArray *filters,
    const gchar * const *cap_tokens,
    GabbleCapabilitySet *cap_set,
    GPtrArray *data_forms)
{
  WockyStanza *stanza;
  WockyDataForm *form;

  if (tp_strdiff (client_name, "dataformtest"))
    return;

  stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_NONE, NULL, "badger",
      '(', "x",
        ':', "jabber:x:data",
        '@', "type", "result",
        '(', "field",
          '@', "var", "FORM_TYPE",
          '@', "type", "hidden",
          '(', "value", '$', "gabble:test:channel:manager:data:form", ')',
        ')',
        '(', "field",
          '@', "var", "animal",
          '(', "value", '$', "badger", ')',
          '(', "value", '$', "snake", ')',
          '(', "value", '$', "weasel", ')',
        ')',
        '(', "field",
          '@', "var", "cheese",
          '(', "value", '$', "omgnothorriblecheese", ')',
        ')',
        '(', "field",
          '@', "var", "favourite_crane",
          '(', "value", '$', "a tall one", ')',
          '(', "value", '$', "a short one", ')',
        ')',
        '(', "field",
          '@', "var", "running_out_of",
          '(', "value", '$', "ideas", ')',
          '(', "value", '$', "cake", ')',
        ')',
      ')',
      NULL);

  form = wocky_data_form_new_from_node (
      wocky_node_get_first_child (wocky_stanza_get_top_node (stanza)),
      NULL);

  g_ptr_array_add (data_forms, form);

  g_object_unref (stanza);
}

static void
channel_manager_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->type_foreach_channel_class = test_channel_manager_type_foreach_channel_class;

  /* not requestable. */
  iface->ensure_channel = NULL;
  iface->create_channel = NULL;
  iface->request_channel = NULL;
  iface->foreach_channel_class = NULL;
}

static void
caps_channel_manager_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  GabbleCapsChannelManagerInterface *iface = g_iface;

  iface->represent_client = test_channel_manager_represent_client;
}
