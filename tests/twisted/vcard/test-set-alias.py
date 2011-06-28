
"""
Test alias setting support.
"""

from servicetest import EventPattern, assertEquals
from gabbletest import exec_test, acknowledge_iq
import constants as cs
import ns

def validate_pep_update(pep_update, expected_nickname):
    publish = pep_update.query.elements(uri=ns.PUBSUB, name='publish').next()
    assertEquals(ns.NICK, publish['node'])
    item = publish.elements(uri=ns.PUBSUB, name='item').next()
    nick = item.elements(uri=ns.NICK, name='nick').next()
    assertEquals(expected_nickname, nick.children[0])

def test(q, bus, conn, stream):
    iq_event = q.expect('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard')

    acknowledge_iq(stream, iq_event.stanza)

    conn.Aliasing.SetAliases({1: 'lala'})

    pep_update = q.expect('stream-iq', iq_type='set', query_ns=ns.PUBSUB, query_name='pubsub')
    validate_pep_update(pep_update, 'lala')
    acknowledge_iq(stream, pep_update.stanza)

    iq_event = q.expect('stream-iq', iq_type='set', query_ns='vcard-temp',
        query_name='vCard')
    acknowledge_iq(stream, iq_event.stanza)

    event = q.expect('dbus-signal', signal='AliasesChanged',
        args=[[(1, u'lala')]])

if __name__ == '__main__':
    exec_test(test)
