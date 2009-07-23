#include "config.h"

#include "src/caps-hash.h"
#include "src/util.h"

static gboolean
check_hash (LmMessage *stanza,
  const gchar *expected)
{
  gchar *hash;

  hash = caps_hash_compute_from_lm_node (stanza->node);
  g_assert (!tp_strdiff (hash, expected));
  lm_message_unref (stanza);
  g_free (hash);
  return TRUE;
}

static gboolean
test_simple (void)
{
  /* Simple example from XEP-0115 */
  LmMessage *stanza = lm_message_build ("badger", LM_MESSAGE_TYPE_IQ,
    '(', "identity", "",
      '@', "category", "client",
      '@', "name", "Exodus 0.9.1",
      '@', "type", "pc",
    ')',
    '(', "feature", "", '@', "var", "http://jabber.org/protocol/disco#info", ')',
    '(', "feature", "", '@', "var", "http://jabber.org/protocol/disco#items", ')',
    '(', "feature", "", '@', "var", "http://jabber.org/protocol/muc", ')',
    '(', "feature", "", '@', "var", "http://jabber.org/protocol/caps", ')',
    ')',
    NULL);

  return check_hash (stanza, "QgayPKawpkPSDYmwT/WM94uAlu0=");
}

int
main (void)
{
  g_type_init ();
  g_assert (test_simple ());

  return 0;
}
