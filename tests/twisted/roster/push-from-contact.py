"""
Ensure that Gabble correctly ignores roster pushes from contacts.
"""

from servicetest import EventPattern
from gabbletest import exec_test, acknowledge_iq
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

    # Some malicious peer sends us a roster push to try to trick us into
    # showing them on our roster. Gabble should know better than to trust it.
    iq = make_roster_push(stream, jid, 'both')
    iq['from'] = jid
    stream.send(iq)

    q.forbid_events(
        [ EventPattern('dbus-signal', signal='MembersChanged',
              path=stored.object_path),
          EventPattern('dbus-signal', signal='ContactsChanged'),
        ])

    e = q.expect('stream-iq', iq_type='error')

if __name__ == '__main__':
    exec_test(test)
