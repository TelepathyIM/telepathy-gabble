
#include "config.h"

#include <string.h>

#include "src/util.h"

static void
test_pass (
    const gchar *jid,
    const gchar *expected_node,
    const gchar *expected_domain,
    const gchar *expected_resource)
{
  gchar *node = NULL;
  gchar *domain = NULL;
  gchar *resource = NULL;

  g_assert (gabble_decode_jid (jid, &node, &domain, &resource));
  g_assert (!tp_strdiff (expected_node, node));
  g_assert (!tp_strdiff (expected_domain, domain));
  g_assert (!tp_strdiff (expected_resource, resource));
  g_free (node);
  g_free (domain);
  g_free (resource);
}

static void
test_fail (const gchar *jid)
{
  gchar *node = NULL;
  gchar *domain = NULL;
  gchar *resource = NULL;

  g_assert (!gabble_decode_jid (jid, &node, &domain, &resource));
  g_assert (node == NULL);
  g_assert (domain == NULL);
  g_assert (resource == NULL);
}

int
main (void)
{
  test_fail ("");
  test_pass ("bar", NULL, "bar", NULL);
  test_pass ("foo@bar", "foo", "bar", NULL);
  test_pass ("foo@bar/baz", "foo", "bar", "baz");
  test_fail ("@bar");
  test_fail ("foo@bar/");
  test_pass ("Foo@Bar/Baz", "foo", "bar", "Baz");
  test_fail ("foo@@");
  test_fail ("foo&bar@baz");
  test_pass ("foo/bar@baz", NULL, "foo", "bar@baz");
  test_pass ("foo@bar/foo@bar/foo@bar", "foo", "bar", "foo@bar/foo@bar");

  return 0;
}

