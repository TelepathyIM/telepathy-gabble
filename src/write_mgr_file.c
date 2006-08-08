#include "gabble-connection-manager.c"

int
main(void)
{
  GKeyFile *f = g_key_file_new();
  const GabbleParamSpec *row;
  GError *error;
  gchar *s;

  g_key_file_set_string(f, "ConnectionManager", "Name", "gabble");
  g_key_file_set_string(f, "ConnectionManager", "BusName", BUS_NAME);
  g_key_file_set_string(f, "ConnectionManager", "ObjectPath", OBJECT_PATH);

  for (row = jabber_params; row->name; row++)
    {
      gchar *param_name = g_strdup_printf("param-%s", row->name);
      gchar *param_value = g_strdup_printf("%s%s%s", row->dtype,
          (row->flags & TP_CONN_MGR_PARAM_FLAG_REQUIRED ? " required" : ""),
          (row->flags & TP_CONN_MGR_PARAM_FLAG_REGISTER ? " register" : ""));
      g_key_file_set_string(f, "Protocol jabber", param_name, param_value);
      g_free(param_value);
      g_free(param_name);

    }

  for (row = jabber_params; row->name; row++)
    {
      if (row->flags & TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT)
        {
          gchar *default_name = g_strdup_printf("default-%s", row->name);

          switch (row->gtype)
            {
            case G_TYPE_STRING:
              g_key_file_set_string(f, "Protocol jabber", default_name,
                                    row->def);
              break;
            case G_TYPE_INT:
            case G_TYPE_UINT:
              g_key_file_set_integer(f, "Protocol jabber", default_name,
                                     GPOINTER_TO_INT(row->def));
              break;
            case G_TYPE_BOOLEAN:
              g_key_file_set_boolean(f, "Protocol jabber", default_name,
                                     GPOINTER_TO_INT(row->def) ? 1 : 0);
            }
          g_free(default_name);
        }
    }

  s = g_key_file_to_data(f, NULL, &error);
  if (!s)
    {
      fprintf(stderr, error->message);
      g_error_free(error);
      return 1;
    }
  printf("%s", s);
  g_free(s);
  return 0;
}
