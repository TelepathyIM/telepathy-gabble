#include <stdlib.h>
#include <telepathy-glib/account.h>
#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>


struct AccountDetails
{
  TpConnectionStatus status;
  TpConnection *connection;
  TpAccount *account;
};

/* This hash table contains all notifyer. */
static GHashTable *account_map;


/* Forward declarations */
static void account_property_changed_cb (TpAccount *proxy, 
    GHashTable *arg_Properties, gpointer user_data, GObject *weak_object);
static void get_connection_cb (TpProxy *proxy, const GValue *out_Value,
    const GError *error, gpointer user_data, GObject *weak_object);
static void get_connection_status_cb (TpProxy *proxy, const GValue *out_Value,
    const GError *error, gpointer user_data, GObject *weak_object);

static void
remove_account (const gchar *object_path)
{
  g_debug ("Account removed: %s", object_path);
  g_hash_table_remove (account_map, object_path);
}

static void
add_account (TpDBusDaemon *bus_daemon, const gchar *_object_path)
{
  gchar *object_path;
  GError *error;
  TpAccount *account;
  TpProxySignalConnection *prop_changed_sc;
  struct AccountDetails *acc_details;

  object_path = g_strdup (_object_path);
  
  error = NULL;
  account = tp_account_new (bus_daemon, object_path, &error);
  if (!account)
    {
      g_printerr ("Failed to create proxy account: %s (obj. path %s)\n", 
          error->message, object_path);
      g_error_free (error);
      return;
    }

  acc_details = g_new0(struct AccountDetails, 1);
  acc_details->status = TP_CONNECTION_STATUS_DISCONNECTED;
  acc_details->account = account;
  g_hash_table_insert (account_map, object_path, acc_details);
  

  error = NULL;
  prop_changed_sc = tp_cli_account_connect_to_account_property_changed (account,
      account_property_changed_cb, acc_details, NULL, NULL, &error);
  if (!prop_changed_sc)
    {
      g_printerr ("Failed to connect account property changed signal: %s\n",
          error->message);
      g_error_free (error);
      remove_account (object_path);
      return;
    }

  tp_cli_dbus_properties_call_get (TP_PROXY (account), 500,
        TP_IFACE_ACCOUNT, "Connection",
        get_connection_cb, acc_details, NULL, NULL);

  tp_cli_dbus_properties_call_get (TP_PROXY (account), 500,
        TP_IFACE_ACCOUNT, "ConnectionStatus",
        get_connection_status_cb, acc_details, NULL, NULL);

  g_debug ("Account added: %s", object_path);
}

static void
account_removed_cb (TpAccountManager *proxy, const gchar *arg_Account, 
    gpointer user_data, GObject *weak_object)
{
  remove_account (arg_Account);
}

static void
account_validity_changed_cb (TpAccountManager *proxy, const gchar *arg_Account,
    gboolean arg_Valid, gpointer user_data, GObject *weak_object)
{
  if (arg_Valid)
    {
      add_account (tp_proxy_get_dbus_daemon (proxy), arg_Account);
    }
  else
    {
      remove_account (arg_Account);
    }
}

static void
get_accounts_cb (TpProxy *proxy, const GValue *out_Value, const GError *error,
    gpointer user_data, GObject *weak_object)
{
  if (error)
    {
      g_printerr ("Failed to get account list: %s\n", 
          error->message);
    }
  else
    {
      guint i;
      const GPtrArray *accounts;
      
      g_return_if_fail (G_VALUE_HOLDS (out_Value,TP_ARRAY_TYPE_OBJECT_PATH_LIST));

      accounts = g_value_get_boxed (out_Value);

      g_debug ("Got %i account%s:", accounts->len, accounts->len > 1 ? "s": "");

      for (i = 0; i < accounts->len; i++)
        {
          const char *account_path = g_ptr_array_index (accounts, i);
          g_debug ("\t%i. %s", i, account_path);
          add_account (tp_proxy_get_dbus_daemon (proxy), account_path);
        }
    }
}

static void
create_connection (TpProxy *proxy, struct AccountDetails *acc_details, const GValue *value)
{
  gchar *con_path;
  
  g_return_if_fail (G_VALUE_HOLDS (value, DBUS_TYPE_G_OBJECT_PATH));
  con_path = g_value_get_boxed (value);

  /* if we have a valid connection path (not "/") then
   * create proxy */
  if (g_strcmp0 (con_path, "/"))
    {
      GError *error = NULL;

      g_return_if_fail (acc_details->connection == NULL);
      g_debug ("Connection received (%s)", con_path);

      acc_details->connection = tp_connection_new (
          tp_proxy_get_dbus_daemon (proxy), tp_proxy_get_bus_name (proxy),
          con_path, &error);
      if (!acc_details->connection)
        {
          g_printerr ("Failed to create connection object: %s\n",
              error->message);
          g_error_free (error);
          remove_account (tp_proxy_get_object_path (proxy));
        }
    }
}


static void
account_property_changed_cb (TpAccount *proxy, GHashTable *arg_Properties, 
    gpointer user_data, GObject *weak_object)
{
  struct AccountDetails *acc_details = user_data;
  GValue *value;


  value = g_hash_table_lookup (arg_Properties, "Connection");
  if (value)
    {
      g_return_if_fail (G_VALUE_HOLDS (value, DBUS_TYPE_G_OBJECT_PATH));
      create_connection (TP_PROXY (proxy), acc_details, value);
    }

  value = g_hash_table_lookup (arg_Properties, "ConnectionStatus");
  if (value)
    {
      const gchar *connection_status_debug_str[] =
        {"CONNECTED", "CONNECTING", "DISCONNECTED"};

      TpConnectionStatus status;
      g_return_if_fail (G_VALUE_HOLDS_UINT (value));
      status = g_value_get_uint (value);

      g_debug ("Account connection satus changed to '%s' (%s)", 
          connection_status_debug_str[status], tp_proxy_get_object_path (proxy));

      /* Make sure status changed, CM may send all properties. */
      if (status != acc_details->status)
        {
          acc_details->status = status;
          if (status == TP_CONNECTION_STATUS_CONNECTED)
            {
              g_debug ("Account connected (%s)", tp_proxy_get_object_path (proxy));
              /* TODO Look for MailNotification interface. */
            }
          if (status == TP_CONNECTION_STATUS_DISCONNECTED)
            {
              g_return_if_fail (acc_details->connection != NULL);
              g_debug ("Account disconnected (%s)", tp_proxy_get_object_path (proxy));
              g_object_unref (acc_details->connection);
              acc_details->connection = NULL;
            }
        }
    }
}

static void
get_connection_cb (TpProxy *proxy, const GValue *out_Value, const GError *request_error,
    gpointer user_data, GObject *weak_object)
{
  struct AccountDetails *acc_details = user_data;

  if (request_error)
    {
      g_printerr ("Failed to get connection on account %s\n",
          tp_proxy_get_object_path (proxy));
      remove_account (tp_proxy_get_object_path (proxy));
      return;
    }

  create_connection (proxy, acc_details, out_Value);
}

static void 
get_connection_status_cb (TpProxy *proxy, const GValue *out_Value,
    const GError *error, gpointer user_data, GObject *weak_object)
{
  struct AccountDetails *acc_details = user_data;

  if (error)
    {
      g_printerr ("Failed to get connectioni status on account %s\n",
          tp_proxy_get_object_path (proxy));
      remove_account (tp_proxy_get_object_path (proxy));
      return;
    }
  
  g_return_if_fail (G_VALUE_HOLDS (out_Value, G_TYPE_UINT));
  acc_details->status = g_value_get_uint (out_Value);
}

static void free_account_details (gpointer arg)
{
  struct AccountDetails *acc_details = arg;
  if (acc_details->connection)
    g_object_unref (acc_details->connection);
  g_object_unref (acc_details->account);
  g_free (acc_details);
}

int
main (int argc, char **argv)
{
  TpDBusDaemon *bus;
  TpAccountManager *account_mgr;
  TpProxySignalConnection *removed_sc, *validity_sc;
  GMainLoop *loop;
  GError *error;

  g_type_init ();

  account_map = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, free_account_details);

  error = NULL;
  bus = tp_dbus_daemon_dup (&error);
  if (!bus)
    {
      g_printerr ("Failed to open connection to bus: %s\n",
          error->message);
      g_error_free (error);
      exit (1);
    }

  account_mgr = tp_account_manager_new (bus);

  error = NULL;
  removed_sc = tp_cli_account_manager_connect_to_account_removed (account_mgr, 
      account_removed_cb, NULL, NULL, NULL, &error);
  if (!removed_sc)
    {
      g_printerr ("Failed to connect to account manager 'removed' signal: %s\n",
          error->message);
      g_error_free (error);
      exit (1);
    }

  error = NULL;
  validity_sc = tp_cli_account_manager_connect_to_account_validity_changed (
      account_mgr, account_validity_changed_cb, NULL, NULL, NULL, &error);
  if (!validity_sc)
    {
      g_printerr ("Failed to connect to account manager 'validity-change' signal: %s\n",
          error->message);
      g_error_free (error);
      exit (1);
    }

  tp_cli_dbus_properties_call_get (TP_PROXY (account_mgr), 500,
        TP_IFACE_ACCOUNT_MANAGER, "ValidAccounts",
        get_accounts_cb, NULL, NULL, NULL);

   
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  return 0;
}
