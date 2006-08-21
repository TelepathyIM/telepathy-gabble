#include <gintset.h>
#include <gheap.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

static gint comparator_fn(gconstpointer a, gconstpointer b)
{
	return (a < b) ? -1 : (a == b) ? 0 : 1;
}

int main()
{
	GHeap *heap = g_heap_new(comparator_fn);
	guint prev = 0;
	guint i;

	srand(time(NULL));

	for (i=0; i<10000; i++)
	{
		g_heap_add(heap, GUINT_TO_POINTER(rand()));
	}

	while (g_heap_size(heap))
	{
		guint elem = GPOINTER_TO_INT(g_heap_peek_first(heap));
		g_assert(elem == GPOINTER_TO_UINT(g_heap_extract_first(heap)));
		g_assert(prev <= elem);
		prev = elem;
	}

	return 0;
}
