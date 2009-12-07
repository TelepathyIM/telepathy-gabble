#include "src/util.h"

static gboolean
test_inherited_ns (void)
{
  LmMessageNode *baz;
  LmMessage *stanza;

  stanza = lm_message_build ("foo", LM_MESSAGE_TYPE_IQ,
    '@', "xmlns", "bar",
      '(', "baz", "", ')',
    NULL);

  g_assert (lm_message_node_has_namespace (stanza->node, "bar", NULL));

  baz = lm_message_node_get_child (stanza->node, "baz");

  g_assert (lm_message_node_has_namespace (baz, "bar", NULL));
  g_assert (lm_message_node_get_child_with_namespace (stanza->node, "baz", "bar") == baz);

  lm_message_unref (stanza);
  return TRUE;
}

static gboolean
test_uninherited_ns (void)
{
  LmMessageNode *foo, *baz;
  LmMessage *stanza;

  stanza = lm_message_build ("blah", LM_MESSAGE_TYPE_IQ,
    '(', "bar:foo", "",
      '@', "xmlns:bar", "zomg",
      '(', "baz", "", ')',
    ')',
    NULL);

  foo = lm_message_node_get_child_with_namespace (stanza->node, "foo", "zomg");
  g_assert (foo != NULL);

  baz = lm_message_node_get_child (foo, "baz");

  g_assert (!lm_message_node_has_namespace (baz, "zomg", NULL));
  g_assert (lm_message_node_get_child_with_namespace (foo, "baz", "zomg") == NULL);

  lm_message_unref (stanza);
  return TRUE;
}

int
main (void)
{
  g_type_init ();

  g_assert (test_inherited_ns ());
  g_assert (test_uninherited_ns ());

  return 0;
}
