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

static gboolean
test_complex (void)
{
  /* Complex example from XEP-0115 */
  LmMessage *stanza = lm_message_build ("badger", LM_MESSAGE_TYPE_IQ,
    '(', "identity", "",
      '@', "category", "client",
      '@', "name", "Psi 0.11",
      '@', "type", "pc",
      '@', "xml:lang", "en",
    ')',
    '(', "identity", "",
      '@', "category", "client",
      '@', "name", "Ψ 0.11",
      '@', "type", "pc",
      '@', "xml:lang", "el",
    ')',
    '(', "feature", "", '@', "var", "http://jabber.org/protocol/disco#info", ')',
    '(', "feature", "", '@', "var", "http://jabber.org/protocol/disco#items", ')',
    '(', "feature", "", '@', "var", "http://jabber.org/protocol/muc", ')',
    '(', "feature", "", '@', "var", "http://jabber.org/protocol/caps", ')',
    '(', "x", "",
      '@', "xmlns", "jabber:x:data",
      '@', "type", "result",
      '(', "field", "",
        '@', "var", "FORM_TYPE",
        '@', "type", "hidden",
        '(', "value", "urn:xmpp:dataforms:softwareinfo", ')',
      ')',
      '(', "field", "",
        '@', "var", "ip_version",
        '(', "value", "ipv4", ')',
        '(', "value", "ipv6", ')',
      ')',
      '(', "field", "",
        '@', "var", "os",
        '(', "value", "Mac", ')',
      ')',
      '(', "field", "",
        '@', "var", "os_version",
        '(', "value", "10.5.1", ')',
      ')',
      '(', "field", "",
        '@', "var", "software",
        '(', "value", "Psi", ')',
      ')',
      '(', "field", "",
        '@', "var", "software_version",
        '(', "value", "0.11", ')',
      ')',
    ')',
    NULL);

  return check_hash (stanza, "q07IKJEyjvHSyhy//CH0CxmKi8w=");
}

int
main (void)
{
  g_type_init ();
  g_assert (test_simple ());
  g_assert (test_complex ());

  return 0;
}
