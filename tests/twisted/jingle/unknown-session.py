"""
Tests that Gabble doesn't explode if it gets Jingle stanzas for unknown
sessions.
"""

from gabbletest import exec_test
from servicetest import assertEquals
from jingletest2 import JingleProtocol031
import ns

def assertHasChild(node, uri, name):
    try:
        node.elements(uri=uri, name=name).next()
    except StopIteration:
        raise AssertionError(
            "Expected <%s xmlns='%s'> to be a child of\n    %s" % (
            name, uri, node.toXml()))

def test_send_action_for_unknown_session(q, bus, conn, stream):
    jp = JingleProtocol031()
    peer = 'guybrush@threepwo.od'

    iq = jp.SetIq(peer, 'test@localhost',
        [ jp.Jingle('fine-leather-jackets', peer, 'session-info', [])
        ])
    stream.send(jp.xml(iq))

    e = q.expect('stream-iq', iq_type='error', iq_id=iq[2]['id'])
    stanza = e.stanza
    error_node = stanza.children[-1]
    assertEquals('error', error_node.name)

    # http://xmpp.org/extensions/xep-0166.html#example-29
    assertHasChild(error_node, ns.STANZA, 'item-not-found')
    assertHasChild(error_node, ns.JINGLE_ERRORS, 'unknown-session')

if __name__ == '__main__':
    exec_test(test_send_action_for_unknown_session)
