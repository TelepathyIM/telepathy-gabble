"""
Regression test for a crash in which PRESENCE_CAP_GOOGLE_VOICE is preloaded
and we don't have any per_channel_manager_caps by the time we receive a
call, when called by gtalk2voip.com on behalf of a sipphone.com user.
"""

import dbus

from gabbletest import make_result_iq, exec_test, sync_stream
from servicetest import (
    make_channel_proxy, unwrap, EventPattern, assertEquals, assertLength)
from jingletest2 import JingleTest2, GtalkProtocol03
import constants as cs
import ns

from twisted.words.xish import xpath

class MyJingleTest(JingleTest2):
    remote_caps = { 'ext': 'sidebar voice-v1',
            'node': 'http://www.google.com/xmpp/client/caps',
            'ver': '1.0.0.104'
            }

def test(q, bus, conn, stream):
    jp = GtalkProtocol03()
    jt = MyJingleTest(jp, conn, q, stream, 'test@localhost', 'foo@gtalk2voip.com')
    jt.prepare(send_presence=False)
    # Send the presence from a bare JID to our bare JID...
    stream.send(jp.xml(jp.Presence('foo@gtalk2voip.com', 'test@localhost',
        jt.remote_caps)))

    event = q.expect('stream-iq', query_ns=ns.DISCO_INFO, to='foo@gtalk2voip.com')
    # ... then immediately send from a bare JID to our full JID
    stream.send(jp.xml(jp.Presence('foo@gtalk2voip.com', 'test@localhost/test',
        jt.remote_caps)))

    stream.send(jp.xml(jp.ResultIq('test@localhost/test', event.stanza,
        [ jp.Query(None, ns.DISCO_INFO,
            [ jp.Feature(x) for x in jp.features ]) ]) ))

    sync_stream(q, stream)

if __name__ == '__main__':
    exec_test(test)
