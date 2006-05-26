#include <glib.h>
#include <gintset.h>

int main (int argc, char **argv)
{
  GIntSet *set1 = g_intset_new();
  GIntSet *a, *b;
  GIntSet *ab_union, *ab_expected_union;
  GIntSet *ab_inter, *ab_expected_inter;
  GIntSet *a_diff_b, *a_expected_diff_b;
  GIntSet *b_diff_a, *b_expected_diff_a;
  GIntSet *ab_symmdiff, *ab_expected_symmdiff;

  g_intset_add(set1, 0);

  g_intset_add(set1, 2);
  g_intset_add(set1, 3);
  g_intset_add(set1, 5);
  g_intset_add(set1, 8);

  g_intset_add(set1, 1024);
  g_intset_add(set1, 32);

  g_assert(g_intset_size(set1) == 7);

  g_assert(g_intset_is_member(set1, 2));
  g_assert(g_intset_is_member(set1, 5));
  g_assert(g_intset_is_member(set1, 1024));
  g_assert(!g_intset_is_member(set1, 1023));
  g_assert(!g_intset_is_member(set1, 1025));
  g_assert(g_intset_is_member(set1, 0));
  g_assert(g_intset_is_member(set1, 32));
  g_assert(!g_intset_is_member(set1, 31));
  g_assert(!g_intset_is_member(set1, 33));

  g_intset_remove(set1, 8);
  g_intset_remove(set1, 1024);
  g_assert(g_intset_size(set1) == 5);

#define NUM_A 11
#define NUM_B 823
#define NUM_C 367
#define NUM_D 4177
#define NUM_E 109
#define NUM_F 1861

  a = g_intset_new ();
  g_intset_add (a, NUM_A);
  g_intset_add (a, NUM_B);
  g_intset_add (a, NUM_C);
  g_intset_add (a, NUM_D);

  g_assert (g_intset_is_equal (a, a));

  b = g_intset_new ();
  g_intset_add (b, NUM_C);
  g_intset_add (b, NUM_D);
  g_intset_add (b, NUM_E);
  g_intset_add (b, NUM_F);

  g_assert (g_intset_is_equal (b, b));
  g_assert (!g_intset_is_equal (a, b));

  ab_expected_union = g_intset_new ();
  g_intset_add (ab_expected_union, NUM_A);
  g_intset_add (ab_expected_union, NUM_B);
  g_intset_add (ab_expected_union, NUM_C);
  g_intset_add (ab_expected_union, NUM_D);
  g_intset_add (ab_expected_union, NUM_E);
  g_intset_add (ab_expected_union, NUM_F);

  ab_union = g_intset_union (a, b);
  g_assert (g_intset_is_equal (ab_union, ab_expected_union));

  ab_expected_inter = g_intset_new ();
  g_intset_add (ab_expected_inter, NUM_C);
  g_intset_add (ab_expected_inter, NUM_D);

  ab_inter = g_intset_intersection (a, b);
  g_assert (g_intset_is_equal (ab_inter, ab_expected_inter));

  a_expected_diff_b = g_intset_new ();
  g_intset_add (a_expected_diff_b, NUM_A);
  g_intset_add (a_expected_diff_b, NUM_B);

  a_diff_b = g_intset_difference (a, b);
  g_assert (g_intset_is_equal (a_diff_b, a_expected_diff_b));

  b_expected_diff_a = g_intset_new ();
  g_intset_add (b_expected_diff_a, NUM_E);
  g_intset_add (b_expected_diff_a, NUM_F);

  b_diff_a = g_intset_difference (b, a);
  g_assert (g_intset_is_equal (b_diff_a, b_expected_diff_a));

  ab_expected_symmdiff = g_intset_new ();
  g_intset_add (ab_expected_symmdiff, NUM_A);
  g_intset_add (ab_expected_symmdiff, NUM_B);
  g_intset_add (ab_expected_symmdiff, NUM_E);
  g_intset_add (ab_expected_symmdiff, NUM_F);

  ab_symmdiff = g_intset_symmetric_difference (a, b);
  g_assert (g_intset_is_equal (ab_symmdiff, ab_expected_symmdiff));

  {
    GArray *arr;
    GIntSet *tmp;

    arr = g_intset_to_array (a);
    tmp = g_intset_from_array (arr);
    g_assert (g_intset_is_equal (a, tmp));
    g_array_free (arr, TRUE);
    g_intset_destroy (tmp);

    arr = g_intset_to_array (b);
    tmp = g_intset_from_array (arr);
    g_assert (g_intset_is_equal (b, tmp));
    g_array_free (arr, TRUE);
    g_intset_destroy (tmp);
  }

  return 0;
}
