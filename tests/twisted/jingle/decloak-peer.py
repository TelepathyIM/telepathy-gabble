"""
Test use-case when client attempts to call an unsubscribed contact. Gabble
should ask them to "de-cloak".
"""

from gabbletest import exec_test
from servicetest import (make_channel_proxy, call_async, sync_dbus,
        assertEquals, assertLength)
from jingletest2 import JingleProtocol031, JingleTest2

import dbus

import constants as cs
import ns

from config import VOIP_ENABLED

if not VOIP_ENABLED:
    print("NOTE: built with --disable-voip")
    raise SystemExit(77)

def test(q, bus, conn, stream):
    jp = JingleProtocol031()
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost',
        'foo2@bar.com/Foo')
    # Make gabble think this is a different client
    jt2.remote_caps['node'] = 'http://example.com/fake-client1'

    run_test(q, bus, conn, stream, jt, True)
    run_test(q, bus, conn, stream, jt2, False)

def run_test(q, bus, conn, stream, jt, decloak_allowed):
    """
    Requests streams on a media channel to jt.remote_jid without having their
    presence at all.
    """

    call_async(q, conn.Requests, 'CreateChannel',
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
          cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
          cs.TARGET_ID: jt.peer,
          cs.CALL_INITIAL_AUDIO: True,
          cs.CALL_INITIAL_VIDEO: False,
        })

    e = q.expect('stream-presence',
            to=jt.peer_bare_jid, presence_type=None)
    nodes = [ node for node in e.stanza.elements(uri=ns.TEMPPRES, name='temppres') ]
    assertLength(1, nodes)
    assertEquals('media', nodes[0].getAttribute('reason'))

    if decloak_allowed:
        jt.send_presence_and_caps()

        # RequestStreams should now happily complete
        q.expect('dbus-return', method='CreateChannel')
    else:
        q.expect('dbus-error', method='CreateChannel',
                name=cs.OFFLINE)

if __name__ == '__main__':
    print("FIXME: needs to be ported to Call1")
    raise SystemExit(77)

    exec_test(test, timeout=10)
