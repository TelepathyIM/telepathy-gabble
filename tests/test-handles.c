#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <handles.h>
#include <telepathy-constants.h>
#include <telepathy-glib/tp-interfaces.h>
#include <telepathy-glib/tp-errors.h>

void test_handles (guint handle_type)
{
  GabbleHandleRepo *repo = NULL;
  GError *error = NULL;

  TpHandle handle = 0;
  const gchar *jid = "handle.test@foobar";
  const gchar *return_jid;

  repo = gabble_handle_repo_new ();
  g_assert (repo != NULL);

  /* Handle zero is never valid */
  g_assert (gabble_handle_is_valid (repo, handle_type, 0, &error) == FALSE);
  g_assert (error->code == TpError_InvalidArgument);

  g_error_free (error);
  error = NULL;

  /* Properly return error when handle isn't in the repo */
  g_assert (gabble_handle_is_valid (repo, handle_type, 65536, &error) == FALSE);
  g_assert (error->code == TpError_InvalidArgument);

  g_error_free (error);
  error = NULL;

  /* Properly return when error out argument isn't provided */
  g_assert (gabble_handle_is_valid (repo, handle_type, 65536, NULL) == FALSE);

  switch (handle_type)
    {
      case TP_HANDLE_TYPE_CONTACT:
          handle = gabble_handle_for_contact (repo, jid, FALSE);
          break;
      case TP_HANDLE_TYPE_ROOM:
          handle = gabble_handle_for_room (repo, jid);
          break;
      case TP_HANDLE_TYPE_LIST:
          jid = "known";
          handle = gabble_handle_for_list (repo, jid);
          break;
    }
  g_assert (handle != 0);

  /* Ref it */
  g_assert (gabble_handle_ref (repo, handle_type, handle) == TRUE);

  /* Try to inspect it */
  return_jid = gabble_handle_inspect (repo, handle_type, handle);
  g_assert (!strcmp (return_jid, jid));

  if (handle_type != TP_HANDLE_TYPE_LIST)
    {
      /* Hold the handle */
      g_assert (gabble_handle_client_hold (repo, "TestSuite", handle, handle_type, NULL) == TRUE);

      /* Now unref it */
      g_assert (gabble_handle_unref (repo, handle_type, handle) == TRUE);

      /* Validate it, should be all healthy because client holds it still */
      g_assert (gabble_handle_is_valid (repo, handle_type, handle, NULL) == TRUE);

      /* Ref it again */
      g_assert (gabble_handle_ref (repo, handle_type, handle) == TRUE);

      /* Client releases it */
      g_assert (gabble_handle_client_release (repo, "TestSuite", handle, handle_type, NULL) == TRUE);
    }

  /* Now unref it */
  g_assert (gabble_handle_unref (repo, handle_type, handle) == TRUE);

  if (handle_type != TP_HANDLE_TYPE_LIST)
    {
      /* Try to unref it again, should fail */
      g_assert (gabble_handle_unref (repo, handle_type, handle) == FALSE);
    }

  gabble_handle_repo_destroy (repo);
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
