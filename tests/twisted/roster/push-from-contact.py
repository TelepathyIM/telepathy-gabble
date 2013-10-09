"""
Ensure that Gabble correctly ignores roster pushes from contacts.
"""

from servicetest import EventPattern
from gabbletest import exec_test, acknowledge_iq
from rostertest import make_roster_push
import ns
import constants as cs

jid = 'moonboots@xsf.lit'

def test(q, bus, conn, stream):
    # Gabble asks for the roster; the server sends back an empty roster.
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    acknowledge_iq(stream, event.stanza)

    q.expect('dbus-signal', signal='ContactListStateChanged', args=[cs.CONTACT_LIST_STATE_SUCCESS])

    # Some malicious peer sends us a roster push to try to trick us into
    # showing them on our roster. Gabble should know better than to trust it.
    iq = make_roster_push(stream, jid, 'both')
    iq['from'] = jid
    stream.send(iq)

    q.forbid_events(
        [
          EventPattern('dbus-signal', signal='ContactsChangedWithID'),
        ])

    q.expect('stream-iq', iq_type='error')

if __name__ == '__main__':
    exec_test(test)
