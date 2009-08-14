
#include <glib.h>

#include "src/util.h"

static gboolean idle_quit_called = FALSE;
static GMainLoop *loop = NULL;
static GObject *object;

static gboolean
idle_quit (gpointer data)
{
  /* This callback should be called exactly once per test. */
  g_assert (idle_quit_called == FALSE);
  idle_quit_called = TRUE;
  g_assert (data == object);
  g_main_loop_quit (loop);
  return FALSE;
}

/* Test 1: Source removed before object finalised. */
static void
test_1 (void)
{
  idle_quit_called = FALSE;
  object = g_object_new (G_TYPE_OBJECT, NULL);
  g_object_set_data (object, "foo", GUINT_TO_POINTER (42));
  loop = g_main_loop_new (NULL, FALSE);

  gabble_idle_add_weak (idle_quit, object);
  g_main_loop_run (loop);

  g_object_unref (object);
  g_main_loop_unref (loop);

  g_assert (idle_quit_called == TRUE);
}

static gboolean
idle_unref (gpointer data)
{
  g_object_unref (G_OBJECT (data));
  return FALSE;
}

/* Test 2: Object finalised before source removed. */
static void
test_2 (void)
{
  idle_quit_called = FALSE;
  object = g_object_new (G_TYPE_OBJECT, NULL);
  g_object_set_data (object, "foo", GUINT_TO_POINTER (42));
  loop = g_main_loop_new (NULL, FALSE);

  g_idle_add (idle_unref, object);
  /* This idle quit shouldn't be called because the previous idle will unref
   * the object and trigger the weak notify.
   */
  gabble_idle_add_weak (idle_quit, object);
  /* This one will be called, however. */
  g_idle_add (idle_quit, object);
  g_main_loop_run (loop);

  g_main_loop_unref (loop);

  g_assert (idle_quit_called == TRUE);
}

int
main (void)
{
  g_type_init ();
  test_1();
  test_2();
  return 0;
}

