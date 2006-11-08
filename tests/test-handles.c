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
  gboolean ret = FALSE;

  GabbleHandle handle;
  const gchar *jid = "handle.test@foobar";
  const gchar *return_jid;

  g_type_init ();

  repo = gabble_handle_repo_new ();
  g_assert (repo != NULL);

  /* Handle zero is never valid */
  ret = gabble_handle_is_valid (repo, TP_HANDLE_TYPE_CONTACT, 0, &error);
  g_assert (ret == FALSE);
  g_assert (error->code == InvalidArgument);

  g_error_free (error);
  error = NULL;

  /* Properly return error when handle isn't in the repo */
  ret = gabble_handle_is_valid (repo, TP_HANDLE_TYPE_CONTACT, 65536, &error);
  g_assert (ret == FALSE);
  g_assert (error->code == InvalidArgument);

  g_error_free (error);
  error = NULL;

  /* Properly return when error out argument isn't provided */
  ret = gabble_handle_is_valid (repo, TP_HANDLE_TYPE_CONTACT, 65536, NULL);
  g_assert (ret == FALSE);

  /* Request a new contact handle */
  handle = gabble_handle_for_contact (repo, jid, FALSE);
  g_assert (handle != 0);

  /* Ref it */
  ret = gabble_handle_ref (repo, TP_HANDLE_TYPE_CONTACT, handle);
  g_assert (ret == TRUE);

  /* Try to inspect it */
  return_jid = gabble_handle_inspect (repo, TP_HANDLE_TYPE_CONTACT, handle);
  g_assert (!strcmp (return_jid, jid));

  /* Hold the handle */
  ret = gabble_handle_client_hold (repo, "TestSuite", handle, TP_HANDLE_TYPE_CONTACT, NULL);
  g_assert (ret == TRUE);

  /* Now unref it */
  ret = gabble_handle_unref (repo, TP_HANDLE_TYPE_CONTACT, handle);
  g_assert (ret == TRUE);

  /* Validate it, should be all healthy because client holds it still */
  ret = gabble_handle_is_valid (repo, TP_HANDLE_TYPE_CONTACT, handle, NULL);
  g_assert (ret == TRUE);

  /* Ref it again */
  ret = gabble_handle_ref (repo, TP_HANDLE_TYPE_CONTACT, handle);
  g_assert (ret == TRUE);

  /* Client releases it */
  ret = gabble_handle_client_release (repo, "TestSuite", TP_HANDLE_TYPE_CONTACT, handle, NULL);

  /* Now unref it */
  ret = gabble_handle_unref (repo, TP_HANDLE_TYPE_CONTACT, handle);
  g_assert (ret == TRUE);

  /* Try to unref it again, should fail */
  ret = gabble_handle_unref (repo, TP_HANDLE_TYPE_CONTACT, handle);
  g_assert (ret == FALSE);

  gabble_handle_repo_destroy (repo);

  return 0;
}
