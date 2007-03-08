#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <handles.h>
#include <gabble-connection.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/errors.h>

void test_handles (guint handle_type)
{
  TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES];
  TpHandleRepoIface *tp_repo = NULL;
  GError *error = NULL;
  guint i;

  TpHandle handle = 0;
  const gchar *jid = "handle.test@foobar";
  const gchar *return_jid;

  for (i = 0; i < NUM_TP_HANDLE_TYPES; i++)
    {
      repos[i] = NULL;
    }
  _gabble_connection_create_handle_repos (NULL, repos);
  tp_repo = repos[handle_type];
  g_assert (tp_repo != NULL);

  /* Handle zero is never valid */
  g_assert (tp_handle_is_valid (tp_repo, 0, &error) == FALSE);
  g_assert (error->code == TP_ERROR_INVALID_ARGUMENT);

  g_error_free (error);
  error = NULL;

  /* Properly return error when handle isn't in the repo */
  g_assert (tp_handle_is_valid (tp_repo, 65536, &error) == FALSE);
  g_assert (error->code == TP_ERROR_INVALID_ARGUMENT);

  g_error_free (error);
  error = NULL;

  /* Properly return when error out argument isn't provided */
  g_assert (tp_handle_is_valid (tp_repo, 65536, NULL) == FALSE);

  if (handle_type == TP_HANDLE_TYPE_LIST)
    {
      /* for the static repo we need a name that's actually valid */
      jid = "deny";
    }
  else
    {
      /* It's not there to start with, unless we're using the static repo */
      handle = tp_handle_lookup (tp_repo, jid, NULL);
      g_assert (handle == 0);
    }
  /* ... but when we call tp_handle_ensure we get a new ref to it */
  handle = tp_handle_ensure (tp_repo, jid, NULL);
  g_assert (handle != 0);

  /* Try to inspect it */
  return_jid = tp_handle_inspect (tp_repo, handle);
  g_assert (!strcmp (return_jid, jid));

  if (handle_type != TP_HANDLE_TYPE_LIST)
    {
      /* Hold the handle */
      g_assert (tp_handle_client_hold (tp_repo, "TestSuite", handle, NULL) == TRUE);

      /* Now unref it */
      g_assert (tp_handle_unref (tp_repo, handle) == TRUE);

      /* Validate it, should be all healthy because client holds it still */
      g_assert (tp_handle_is_valid (tp_repo, handle, NULL) == TRUE);

      /* Ref it again */
      g_assert (tp_handle_ref (tp_repo, handle) == TRUE);

      /* Client releases it */
      g_assert (tp_handle_client_release (tp_repo, "TestSuite", handle, NULL) == TRUE);
    }

  /* Now unref it */
  g_assert (tp_handle_unref (tp_repo, handle) == TRUE);

  if (handle_type != TP_HANDLE_TYPE_LIST)
    {
      /* Try to unref it again, should fail */
      g_assert (tp_handle_unref (tp_repo, handle) == FALSE);
    }

  for (i = 0; i < NUM_TP_HANDLE_TYPES; i++)
    {
      if (repos[i])
        g_object_unref((GObject *)repos[i]);
    }
}

int main (int argc, char **argv)
{
  g_type_init ();

  g_debug ("Testing contact handles");
  test_handles (TP_HANDLE_TYPE_CONTACT);
  g_debug ("Testing room handles");
  test_handles (TP_HANDLE_TYPE_ROOM);
  g_debug ("Testing list handles");
  test_handles (TP_HANDLE_TYPE_LIST);
  return 0;
}
