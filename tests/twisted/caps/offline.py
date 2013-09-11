"""
Test for fd.o#32874 -- Offline contacts do not have capabilities.
"""

from gabbletest import exec_test
from servicetest import assertEquals, assertSameSets, assertLength
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    # bob is offline
    jid = 'bob@foo.com'

    event = q.expect('stream-iq', query_ns=ns.ROSTER)

    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = jid
    item['subscription'] = 'from'

    stream.send(event.stanza)

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),

    bob_handle = conn.RequestHandles(cs.HT_CONTACT, [jid])[0]

    # new ContactCapabilities
    ccaps_map = conn.ContactCapabilities.GetContactCapabilities([bob_handle])
    assertLength(1, ccaps_map)

    assertLength(1, ccaps_map[bob_handle])

    fixed, allowed = ccaps_map[bob_handle][0]

    assertEquals({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
                  cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT}, fixed)
    assertSameSets([cs.TARGET_HANDLE], allowed)

if __name__ == '__main__':
    exec_test(test, do_connect=False)
