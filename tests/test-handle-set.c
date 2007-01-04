#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <telepathy-glib/tp-intset.h>
#include <handles.h>
#include <handle-set.h>
#include <telepathy-glib/tp-enums.h>
#include <telepathy-glib/tp-interfaces.h>
#include <telepathy-glib/tp-errors.h>

int main (int argc, char **argv)
{

  GabbleHandleRepo *repo = NULL;
  GabbleHandleSet *set = NULL;
  TpIntSet *iset = NULL;

  TpHandle h1, h2, h3, h4;

  g_type_init ();

  repo = gabble_handle_repo_new ();
  g_assert (repo != NULL);

  set = handle_set_new (repo, TP_HANDLE_TYPE_CONTACT);
  g_assert (set != NULL);

  h1 = gabble_handle_for_contact (repo, "h1@foo", FALSE);
  h2 = gabble_handle_for_contact (repo, "h2@foo", FALSE);
  h3 = gabble_handle_for_contact (repo, "h3@foo", FALSE);
  h4 = gabble_handle_for_contact (repo, "h4@foo", FALSE);
  g_assert (h1 && h2 && h3 && h4);

  /* Add one handle, check that it's in, check the size */
  handle_set_add (set, h1);
  g_assert (handle_set_is_member (set, h1));
  g_assert (handle_set_size (set) == 1);

  /* Adding it again should be no-op */
  handle_set_add (set, h1);
  g_assert (handle_set_size (set) == 1);

  /* Removing a non-member should fail */
  g_assert (handle_set_remove (set, h2) == FALSE);

  /* Add some members via _update() */
  iset = tp_intset_new ();
  tp_intset_add (iset, h1);
  tp_intset_add (iset, h2);
  tp_intset_add (iset, h3);
  iset = handle_set_update (set, iset);
  
  /* h2 and h3 should be added, and h1 not */
  g_assert (!tp_intset_is_member (iset, h1));
  g_assert (tp_intset_is_member (iset, h2));
  g_assert (tp_intset_is_member (iset, h3));
  tp_intset_destroy (iset);
  
  g_assert (handle_set_is_member (set, h2));
  g_assert (handle_set_is_member (set, h3));
  
  /* Remove some members via _update_difference() */
  iset = tp_intset_new ();
  tp_intset_add (iset, h1);
  tp_intset_add (iset, h4);
  iset = handle_set_difference_update (set, iset);
  
  /* h1 should be removed, h4 not */
  g_assert (tp_intset_is_member (iset, h1));
  g_assert (!tp_intset_is_member (iset, h4));
  tp_intset_destroy (iset);
  
  /* Removing a member should succeed */
  g_assert (handle_set_remove (set, h2) == TRUE);
  
  /* Finally, only h3 should be in the set */
  g_assert (handle_set_is_member (set, h3));
  g_assert (handle_set_size (set) == 1);
  
  g_assert (handle_set_remove (set, h3) == TRUE);
  handle_set_destroy (set);

  gabble_handle_repo_destroy (repo);

  return 0;
}
