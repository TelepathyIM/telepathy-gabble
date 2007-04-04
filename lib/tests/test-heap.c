#include <telepathy-glib/intset.h>
#include <telepathy-glib/heap.h>

#include <stdlib.h>
#include <time.h>
#include <stdio.h>

static gint comparator_fn (gconstpointer a, gconstpointer b)
{
    return (a < b) ? -1 : (a == b) ? 0 : 1;
}

int main ()
{
  TpHeap *heap = tp_heap_new (comparator_fn, NULL);
  guint prev = 0;
  guint i;

  srand (time (NULL));

  for (i=0; i<10000; i++)
    {
      tp_heap_add (heap, GUINT_TO_POINTER (rand ()));
    }

  while (tp_heap_size (heap))
    {
      guint elem = GPOINTER_TO_INT (tp_heap_peek_first (heap));
      g_assert (elem == GPOINTER_TO_UINT (tp_heap_extract_first (heap)));
      g_assert (prev <= elem);
      prev = elem;
    }

  return 0;
}
