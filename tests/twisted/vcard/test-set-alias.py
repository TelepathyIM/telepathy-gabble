
"""
Test alias setting support.
"""

from servicetest import EventPattern, assertEquals, assertLength
from gabbletest import (
    exec_test, acknowledge_iq, expect_and_handle_get_vcard, expect_and_handle_set_vcard,
)
import constants as cs
import ns

def validate_pep_update(pep_update, expected_nickname):
    publish = pep_update.query.elements(uri=ns.PUBSUB, name='publish').next()
    assertEquals(ns.NICK, publish['node'])
    item = publish.elements(uri=ns.PUBSUB, name='item').next()
    nick = item.elements(uri=ns.NICK, name='nick').next()

    if expected_nickname is not None:
        assertEquals(expected_nickname, nick.children[0])
    else:
        assertLength(0, nick.children)

def test(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")
    conn.Aliasing.SetAliases({self_handle: 'lala'})
    expect_and_handle_get_vcard(q, stream)

    pep_update = q.expect('stream-iq', iq_type='set', query_ns=ns.PUBSUB, query_name='pubsub')
    validate_pep_update(pep_update, 'lala')
    acknowledge_iq(stream, pep_update.stanza)

    def check(vCard):
        nickname = vCard.elements(uri=ns.VCARD_TEMP, name='NICKNAME').next()
        assertEquals('lala', nickname.children[0])
    expect_and_handle_set_vcard(q, stream, check=check)

    event = q.expect('dbus-signal', signal='AliasesChanged',
        args=[[(self_handle, u'lala')]])

    conn.Aliasing.SetAliases({self_handle: ''})
    pep_update = q.expect('stream-iq', iq_type='set', query_ns=ns.PUBSUB, query_name='pubsub')
    validate_pep_update(pep_update, None)

    expect_and_handle_get_vcard(q, stream)
    def check(vCard):
        # The vCard should be empty, rather than having an empty <NICKNAME/>
        # element.
        assertLength(0, vCard.children)
    expect_and_handle_set_vcard(q, stream, check=check)

    event = q.expect('dbus-signal', signal='AliasesChanged',
        args=[[(self_handle, u'test@localhost')]])

if __name__ == '__main__':
    exec_test(test)
