#include <glib.h>
#include <gintset.h>

int main (int argc, char **argv)
{
  GIntSet *set1 = g_intset_new();
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
  return 0;
}
