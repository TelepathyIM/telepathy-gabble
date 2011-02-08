"""
Ensure that Gabble correctly handles broken roster pushes from servers that
omit id='', in flagrant violation of XMPP Core. (Sadly these servers exist, so
we should interop with them. Think of it of like No_Reply in D-Bus...
"""

from servicetest import EventPattern, sync_dbus
from gabbletest import exec_test, acknowledge_iq, sync_stream
from rostertest import (
    make_roster_push, expect_contact_list_signals, check_contact_list_signals,
)
import ns
import constants as cs

jid = 'moonboots@xsf.lit'

def test(q, bus, conn, stream):
    # Gabble asks for the roster; the server sends back an empty roster.
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    acknowledge_iq(stream, event.stanza)

    pairs = expect_contact_list_signals(q, bus, conn,
            ['stored'])
    stored = check_contact_list_signals(q, bus, conn, pairs.pop(0),
            cs.HT_LIST, 'stored', [])

    # The server sends us a roster push without an id=''. WTF!
    iq = make_roster_push(stream, jid, 'both')
    del iq['id']
    stream.send(iq)

    h = conn.RequestHandles(cs.HT_CONTACT, [jid])[0]
    q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged',
            args=['', [h], [], [], [], 0, 0], path=stored.object_path),
        EventPattern('dbus-signal', signal='ContactsChanged',
            args=[{ h: (cs.SUBSCRIPTION_STATE_YES,
                    cs.SUBSCRIPTION_STATE_YES, ''), },
                []],
            ),
        )

    # Verify that Gabble didn't crash while trying to ack the push.
    sync_stream(q, stream)

    # Just for completeness, let's repeat this test with a malicious roster
    # push from a contact (rather than from our server). Our server's *really*
    # broken if it allows this. Nonetheless...
    iq = make_roster_push(stream, 'silvio@gov.it', 'both')
    del iq['id']
    iq['from'] = 'silvio@gov.it'
    stream.send(iq)

    q.forbid_events(
        [ EventPattern('dbus-signal', signal='MembersChanged',
              path=stored.object_path),
          EventPattern('dbus-signal', signal='ContactsChanged'),
        ])
    # Make sure Gabble's got the evil push...
    sync_stream(q, stream)
    # ...and make sure it's not emitted anything.
    sync_dbus(bus, q, conn)

if __name__ == '__main__':
    exec_test(test)
