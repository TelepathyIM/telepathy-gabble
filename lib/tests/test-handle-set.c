#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <telepathy-glib/intset.h>
#include <telepathy-glib/handle-repo.h>
#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/errors.h>

int main (int argc, char **argv)
{

  TpHandleRepoIface *repo = NULL;
  TpHandleSet *set = NULL;
  TpIntSet *iset = NULL;

  TpHandle h1, h2, h3, h4;

  g_type_init ();

  repo = (TpHandleRepoIface *)g_object_new (TP_TYPE_DYNAMIC_HANDLE_REPO,
      "handle-type", TP_HANDLE_TYPE_CONTACT, NULL);
  g_assert (repo != NULL);

  set = tp_handle_set_new (repo);
  g_assert (set != NULL);

  h1 = tp_handle_request (repo, "h1@foo", TRUE);
  h2 = tp_handle_request (repo, "h2@foo", TRUE);
  h3 = tp_handle_request (repo, "h3@foo", TRUE);
  h4 = tp_handle_request (repo, "h4@foo", TRUE);
  g_assert (h1 && h2 && h3 && h4);

  /* Add one handle, check that it's in, check the size */
  tp_handle_set_add (set, h1);
  g_assert (tp_handle_set_is_member (set, h1));
  g_assert (tp_handle_set_size (set) == 1);

  /* Adding it again should be no-op */
  tp_handle_set_add (set, h1);
  g_assert (tp_handle_set_size (set) == 1);

  /* Removing a non-member should fail */
  g_assert (tp_handle_set_remove (set, h2) == FALSE);

  /* Add some members via _update() */
  iset = tp_intset_new ();
  tp_intset_add (iset, h1);
  tp_intset_add (iset, h2);
  tp_intset_add (iset, h3);
  iset = tp_handle_set_update (set, iset);

  /* h2 and h3 should be added, and h1 not */
  g_assert (!tp_intset_is_member (iset, h1));
  g_assert (tp_intset_is_member (iset, h2));
  g_assert (tp_intset_is_member (iset, h3));
  tp_intset_destroy (iset);

  g_assert (tp_handle_set_is_member (set, h2));
  g_assert (tp_handle_set_is_member (set, h3));

  /* Remove some members via _update_difference() */
  iset = tp_intset_new ();
  tp_intset_add (iset, h1);
  tp_intset_add (iset, h4);
  iset = tp_handle_set_difference_update (set, iset);

  /* h1 should be removed, h4 not */
  g_assert (tp_intset_is_member (iset, h1));
  g_assert (!tp_intset_is_member (iset, h4));
  tp_intset_destroy (iset);

  /* Removing a member should succeed */
  g_assert (tp_handle_set_remove (set, h2) == TRUE);

  /* Finally, only h3 should be in the set */
  g_assert (tp_handle_set_is_member (set, h3));
  g_assert (tp_handle_set_size (set) == 1);

  g_assert (tp_handle_set_remove (set, h3) == TRUE);
  tp_handle_set_destroy (set);

  g_object_unref (G_OBJECT (repo));

  return 0;
}
