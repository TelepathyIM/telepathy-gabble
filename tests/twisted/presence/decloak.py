from twisted.words.xish import domish

from gabbletest import exec_test, make_presence
from servicetest import EventPattern, assertEquals
import ns
import constants as cs

def test(q, bus, conn, stream, should_decloak=False):
    event = q.expect('stream-iq', query_ns=ns.ROSTER)

    event.stanza['type'] = 'result'
    stream.send(event.stanza)

    # First test is to use the CM param's value
    worker(q, bus, conn, stream, should_decloak)

    # We can change it at runtime, so flip it to the other value and retry
    should_decloak = not should_decloak
    conn.Set(cs.CONN_IFACE_GABBLE_DECLOAK, 'DecloakAutomatically',
            should_decloak, dbus_interface=cs.PROPERTIES_IFACE)
    worker(q, bus, conn, stream, should_decloak)

    # Trivial test for SendDirectedPresence()
    bob_handle = conn.get_contact_handle_sync('bob@foo.com')
    conn.SendDirectedPresence(bob_handle, False,
            dbus_interface=cs.CONN_IFACE_GABBLE_DECLOAK)
    q.expect('stream-presence', to='bob@foo.com')

def worker(q, bus, conn, stream, should_decloak):
    decloak_automatically = conn.Get(cs.CONN_IFACE_GABBLE_DECLOAK,
            'DecloakAutomatically', dbus_interface=cs.PROPERTIES_IFACE)
    assertEquals(should_decloak, decloak_automatically)

    amy_handle = conn.get_contact_handle_sync('amy@foo.com')

    # Amy directs presence to us

    presence = make_presence('amy@foo.com/panopticon')
    decloak = presence.addElement((ns.TEMPPRES, 'temppres'))
    decloak['reason'] = 'media'
    stream.send(presence)

    events = [
            EventPattern('dbus-signal', signal='PresencesChanged',
                args=[{amy_handle: (cs.PRESENCE_AVAILABLE, 'available', '')}]),
            EventPattern('dbus-signal', signal='DecloakRequested',
                args=[amy_handle, 'media', should_decloak]),
            ]
    forbidden = []

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

    q.unforbid_events(forbidden)

if __name__ == '__main__':
    exec_test(test,
        params={cs.CONN_IFACE_GABBLE_DECLOAK + '.DecloakAutomatically': False})
    exec_test(lambda q, b, c, s: test(q, b, c, s, should_decloak=True),
        params={cs.CONN_IFACE_GABBLE_DECLOAK + '.DecloakAutomatically': True})
