
#include "config.h"

#include <string.h>

#include "src/util.h"

static void
test1 (void)
{
  gchar *node = NULL;
  gchar *domain = NULL;
  gchar *resource = NULL;

  g_assert (!gabble_decode_jid ("", &node, &domain, &resource));
  g_assert (node == NULL);
  g_assert (domain == NULL);
  g_assert (resource == NULL);
  g_free (domain);
}

static void
test2 (void)
{
  gchar *node = NULL;
  gchar *domain = NULL;
  gchar *resource = NULL;

  g_assert (gabble_decode_jid ("bar", &node, &domain, &resource));
  g_assert (node == NULL);
  g_assert (0 == strcmp (domain, "bar"));
  g_assert (resource == NULL);
  g_free (domain);
}

static void
test3 (void)
{
  gchar *node = NULL;
  gchar *domain = NULL;
  gchar *resource = NULL;

  g_assert (gabble_decode_jid ("foo@bar", &node, &domain, &resource));
  g_assert (0 == strcmp (node, "foo"));
  g_assert (0 == strcmp (domain, "bar"));
  g_assert (resource == NULL);
  g_free (node);
  g_free (domain);
}

static void
test4 (void)
{
  gchar *node = NULL;
  gchar *domain = NULL;
  gchar *resource = NULL;

  g_assert (gabble_decode_jid ("foo@bar/baz", &node, &domain, &resource));
  g_assert (0 == strcmp (node, "foo"));
  g_assert (0 == strcmp (domain, "bar"));
  g_assert (0 == strcmp (resource, "baz"));
  g_free (node);
  g_free (domain);
  g_free (resource);
}

static void
test5 (void)
{
  gchar *node = NULL;
  gchar *domain = NULL;
  gchar *resource = NULL;

  g_assert (!gabble_decode_jid ("@bar", &node, &domain, &resource));
  g_assert (node == NULL);
  g_assert (domain == NULL);
  g_assert (resource == NULL);
  g_free (node);
  g_free (domain);
  g_free (resource);
}

static void
test6 (void)
{
  gchar *node = NULL;
  gchar *domain = NULL;
  gchar *resource = NULL;

  g_assert (!gabble_decode_jid ("foo@bar/", &node, &domain, &resource));
  g_assert (node == NULL);
  g_assert (domain == NULL);
  g_assert (resource == NULL);
  g_free (node);
  g_free (domain);
  g_free (resource);
}

int
main (void)
{
  test1 ();
  test2 ();
  test3 ();
  test4 ();
  test5 ();
  test6 ();

  return 0;
}

