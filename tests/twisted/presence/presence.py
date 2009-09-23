"""
A simple smoke-test for C.I.SimplePresence

FIXME: test C.I.Presence too
"""

from twisted.words.xish import domish

from gabbletest import exec_test, make_presence
from servicetest import EventPattern
import ns
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    _, event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', query_ns=ns.ROSTER),
        )

    amy_handle = conn.RequestHandles(1, ['amy@foo.com'])[0]

    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'

    stream.send(event.stanza)
    stream.send(make_presence('amy@foo.com', show='away', status='At the pub'))

    q.expect('dbus-signal', signal='PresencesChanged',
        args=[{amy_handle: (cs.PRESENCE_AWAY, 'away', 'At the pub')}])

    stream.send(make_presence(
        'amy@foo.com', show='chat', status='I may have been drinking'))

    q.expect('dbus-signal', signal='PresencesChanged',
        args=[{amy_handle:
            (cs.PRESENCE_AVAILABLE, 'chat', 'I may have been drinking')}])

if __name__ == '__main__':
    exec_test(test)
