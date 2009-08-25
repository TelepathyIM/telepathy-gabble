
#include "config.h"

#include <string.h>

#include "src/util.h"

static void
test1 (void)
{
  gchar *node = NULL;
  gchar *server = NULL;
  gchar *resource = NULL;

  g_assert (gabble_decode_jid ("", &node, &server, &resource));
  g_assert (node == NULL);
  g_assert (0 == strcmp (server, ""));
  g_assert (resource == NULL);
  g_free (server);
}

static void
test2 (void)
{
  gchar *node = NULL;
  gchar *server = NULL;
  gchar *resource = NULL;

  g_assert (gabble_decode_jid ("bar", &node, &server, &resource));
  g_assert (node == NULL);
  g_assert (0 == strcmp (server, "bar"));
  g_assert (resource == NULL);
  g_free (server);
}

static void
test3 (void)
{
  gchar *node = NULL;
  gchar *server = NULL;
  gchar *resource = NULL;

  g_assert (gabble_decode_jid ("foo@bar", &node, &server, &resource));
  g_assert (0 == strcmp (node, "foo"));
  g_assert (0 == strcmp (server, "bar"));
  g_assert (resource == NULL);
  g_free (node);
  g_free (server);
}

static void
test4 (void)
{
  gchar *node = NULL;
  gchar *server = NULL;
  gchar *resource = NULL;

  g_assert (gabble_decode_jid ("foo@bar/baz", &node, &server, &resource));
  g_assert (0 == strcmp (node, "foo"));
  g_assert (0 == strcmp (server, "bar"));
  g_assert (0 == strcmp (resource, "baz"));
  g_free (node);
  g_free (server);
  g_free (resource);
}

int
main (void)
{
  test1 ();
  test2 ();
  test3 ();
  test4 ();

  return 0;
}

