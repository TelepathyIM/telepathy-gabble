#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <handles.h>
#include <telepathy-constants.h>
#include <telepathy-interfaces.h>
#include <telepathy-errors.h>

int main (int argc, char **argv)
{

  GabbleHandleRepo *repo = NULL;
  GError *error = NULL;

  GabbleHandle handle;
  const gchar *jid = "handle.test@foobar";
  const gchar *return_jid;

  g_type_init ();

  repo = gabble_handle_repo_new ();
  g_assert (repo != NULL);

  /* Handle zero is never valid */
  g_assert (gabble_handle_is_valid (repo, TP_HANDLE_TYPE_CONTACT, 0, &error) == FALSE);
  g_assert (error->code == InvalidArgument);

  g_error_free (error);
  error = NULL;

  /* Properly return error when handle isn't in the repo */
  g_assert (gabble_handle_is_valid (repo, TP_HANDLE_TYPE_CONTACT, 65536, &error) == FALSE);
  g_assert (error->code == InvalidArgument);

  g_error_free (error);
  error = NULL;

  /* Properly return when error out argument isn't provided */
  g_assert (gabble_handle_is_valid (repo, TP_HANDLE_TYPE_CONTACT, 65536, NULL) == FALSE);

  /* Request a new contact handle */
  handle = gabble_handle_for_contact (repo, jid, FALSE);
  g_assert (handle != 0);

  /* Ref it */
  g_assert (gabble_handle_ref (repo, TP_HANDLE_TYPE_CONTACT, handle) == TRUE);

  /* Try to inspect it */
  return_jid = gabble_handle_inspect (repo, TP_HANDLE_TYPE_CONTACT, handle);
  g_assert (!strcmp (return_jid, jid));

  /* Hold the handle */
  g_assert (gabble_handle_client_hold (repo, "TestSuite", handle, TP_HANDLE_TYPE_CONTACT, NULL) == TRUE);

  /* Now unref it */
  g_assert (gabble_handle_unref (repo, TP_HANDLE_TYPE_CONTACT, handle) == TRUE);

  /* Validate it, should be all healthy because client holds it still */
  g_assert (gabble_handle_is_valid (repo, TP_HANDLE_TYPE_CONTACT, handle, NULL) == TRUE);

  /* Ref it again */
  g_assert (gabble_handle_ref (repo, TP_HANDLE_TYPE_CONTACT, handle) == TRUE);

  /* Client releases it */
  g_assert (gabble_handle_client_release (repo, "TestSuite", TP_HANDLE_TYPE_CONTACT, handle, NULL) == TRUE);

  /* Now unref it */
  g_assert (gabble_handle_unref (repo, TP_HANDLE_TYPE_CONTACT, handle) == TRUE);

  /* Try to unref it again, should fail */
  g_assert (gabble_handle_unref (repo, TP_HANDLE_TYPE_CONTACT, handle) == FALSE);

  gabble_handle_repo_destroy (repo);

  return 0;
}
