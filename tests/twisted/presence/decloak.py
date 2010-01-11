from twisted.words.xish import domish

from gabbletest import exec_test, make_presence
from servicetest import EventPattern
import ns
import constants as cs

def test(q, bus, conn, stream, should_decloak=False):
    conn.Connect()
    _, event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', query_ns=ns.ROSTER),
        )

    event.stanza['type'] = 'result'
    stream.send(event.stanza)

    amy_handle = conn.RequestHandles(1, ['amy@foo.com'])[0]

    # Amy directs presence to us

    presence = make_presence('amy@foo.com/panopticon')
    decloak = presence.addElement((ns.DECLOAK, 'decloak'))
    decloak['reason'] = 'media'
    stream.send(presence)

    events = [
            EventPattern('dbus-signal', signal='PresencesChanged',
                args=[{amy_handle: (cs.PRESENCE_AVAILABLE, 'available', '')}]),
            ]

    if should_decloak:
        events.append(EventPattern('stream-presence',
            to='amy@foo.com/panopticon'))
    else:
        forbidden = [EventPattern('stream-presence')]
        q.forbid_events(forbidden)

    q.expect_many(*events)

    presence = make_presence('amy@foo.com/panopticon', type='unavailable')
    stream.send(presence)
    q.expect('dbus-signal', signal='PresencesChanged',
                args=[{amy_handle: (cs.PRESENCE_OFFLINE, 'offline', '')}])

if __name__ == '__main__':
    exec_test(test)
    exec_test(lambda q, b, c, s: test(q, b, c, s, should_decloak=True),
        params={cs.CONN_IFACE_GABBLE_DECLOAK + '.DecloakAutomatically': True})
