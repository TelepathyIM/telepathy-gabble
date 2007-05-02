
#include <string.h>

#include "util.h"

static gboolean
test1 (void)
{
  gchar *node = NULL;
  gchar *server = NULL;
  gchar *resource = NULL;

  gabble_decode_jid ("", &node, &server, &resource);
  g_assert (node == NULL);
  g_assert (0 == strcmp (server, ""));
  g_assert (resource == NULL);
  g_free (server);
  return TRUE;
}

static gboolean
test2 (void)
{
  gchar *node = NULL;
  gchar *server = NULL;
  gchar *resource = NULL;

  gabble_decode_jid ("bar", &node, &server, &resource);
  g_assert (node == NULL);
  g_assert (0 == strcmp (server, "bar"));
  g_assert (resource == NULL);
  g_free (server);
  return TRUE;
}

static gboolean
test3 (void)
{
  gchar *node = NULL;
  gchar *server = NULL;
  gchar *resource = NULL;

  gabble_decode_jid ("foo@bar", &node, &server, &resource);
  g_assert (0 == strcmp (node, "foo"));
  g_assert (0 == strcmp (server, "bar"));
  g_assert (resource == NULL);
  g_free (node);
  g_free (server);
  return TRUE;
}

static gboolean
test4 (void)
{
  gchar *node = NULL;
  gchar *server = NULL;
  gchar *resource = NULL;

  gabble_decode_jid ("foo@bar/baz", &node, &server, &resource);
  g_assert (0 == strcmp (node, "foo"));
  g_assert (0 == strcmp (server, "bar"));
  g_assert (0 == strcmp (resource, "baz"));
  g_free (node);
  g_free (server);
  g_free (resource);
  return TRUE;
}

int
main (void)
{
  g_assert (test1 ());
  g_assert (test2 ());
  g_assert (test3 ());
  g_assert (test4 ());

  return 0;
}

