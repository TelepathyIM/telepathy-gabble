#include <glib.h>
#include <telepathy-glib/intset.h>

int main (int argc, char **argv)
{
  TpIntSet *set1 = tp_intset_new ();
  TpIntSet *a, *b;
  TpIntSet *ab_union, *ab_expected_union;
  TpIntSet *ab_inter, *ab_expected_inter;
  TpIntSet *a_diff_b, *a_expected_diff_b;
  TpIntSet *b_diff_a, *b_expected_diff_a;
  TpIntSet *ab_symmdiff, *ab_expected_symmdiff;

  tp_intset_add (set1, 0);

  tp_intset_add (set1, 2);
  tp_intset_add (set1, 3);
  tp_intset_add (set1, 5);
  tp_intset_add (set1, 8);

  tp_intset_add (set1, 1024);
  tp_intset_add (set1, 32);

  g_assert (tp_intset_size (set1) == 7);

  g_assert (tp_intset_is_member (set1, 2));
  g_assert (tp_intset_is_member (set1, 5));
  g_assert (tp_intset_is_member (set1, 1024));
  g_assert (!tp_intset_is_member (set1, 1023));
  g_assert (!tp_intset_is_member (set1, 1025));
  g_assert (tp_intset_is_member (set1, 0));
  g_assert (tp_intset_is_member (set1, 32));
  g_assert (!tp_intset_is_member (set1, 31));
  g_assert (!tp_intset_is_member (set1, 33));

  tp_intset_remove (set1, 8);
  tp_intset_remove (set1, 1024);
  g_assert (tp_intset_size (set1) == 5);

#define NUM_A 11
#define NUM_B 823
#define NUM_C 367
#define NUM_D 4177
#define NUM_E 109
#define NUM_F 1861

  a = tp_intset_new ();
  tp_intset_add (a, NUM_A);
  tp_intset_add (a, NUM_B);
  tp_intset_add (a, NUM_C);
  tp_intset_add (a, NUM_D);

  g_assert (tp_intset_is_equal (a, a));

  b = tp_intset_new ();
  tp_intset_add (b, NUM_C);
  tp_intset_add (b, NUM_D);
  tp_intset_add (b, NUM_E);
  tp_intset_add (b, NUM_F);

  g_assert (tp_intset_is_equal (b, b));
  g_assert (!tp_intset_is_equal (a, b));

  ab_expected_union = tp_intset_new ();
  tp_intset_add (ab_expected_union, NUM_A);
  tp_intset_add (ab_expected_union, NUM_B);
  tp_intset_add (ab_expected_union, NUM_C);
  tp_intset_add (ab_expected_union, NUM_D);
  tp_intset_add (ab_expected_union, NUM_E);
  tp_intset_add (ab_expected_union, NUM_F);

  ab_union = tp_intset_union (a, b);
  g_assert (tp_intset_is_equal (ab_union, ab_expected_union));

  ab_expected_inter = tp_intset_new ();
  tp_intset_add (ab_expected_inter, NUM_C);
  tp_intset_add (ab_expected_inter, NUM_D);

  ab_inter = tp_intset_intersection (a, b);
  g_assert (tp_intset_is_equal (ab_inter, ab_expected_inter));

  a_expected_diff_b = tp_intset_new ();
  tp_intset_add (a_expected_diff_b, NUM_A);
  tp_intset_add (a_expected_diff_b, NUM_B);

  a_diff_b = tp_intset_difference (a, b);
  g_assert (tp_intset_is_equal (a_diff_b, a_expected_diff_b));

  b_expected_diff_a = tp_intset_new ();
  tp_intset_add (b_expected_diff_a, NUM_E);
  tp_intset_add (b_expected_diff_a, NUM_F);

  b_diff_a = tp_intset_difference (b, a);
  g_assert (tp_intset_is_equal (b_diff_a, b_expected_diff_a));

  ab_expected_symmdiff = tp_intset_new ();
  tp_intset_add (ab_expected_symmdiff, NUM_A);
  tp_intset_add (ab_expected_symmdiff, NUM_B);
  tp_intset_add (ab_expected_symmdiff, NUM_E);
  tp_intset_add (ab_expected_symmdiff, NUM_F);

  ab_symmdiff = tp_intset_symmetric_difference (a, b);
  g_assert (tp_intset_is_equal (ab_symmdiff, ab_expected_symmdiff));

  {
    GArray *arr;
    TpIntSet *tmp;

    arr = tp_intset_to_array (a);
    tmp = tp_intset_from_array (arr);
    g_assert (tp_intset_is_equal (a, tmp));
    g_array_free (arr, TRUE);
    tp_intset_destroy (tmp);

    arr = tp_intset_to_array (b);
    tmp = tp_intset_from_array (arr);
    g_assert (tp_intset_is_equal (b, tmp));
    g_array_free (arr, TRUE);
    tp_intset_destroy (tmp);
  }

  return 0;
}
