"""
Test for fd.o#32874
"""

from gabbletest import exec_test
from servicetest import assertEquals, assertSameSets
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
    assertEquals(1, len(ccaps_map))

    assertEquals(1, len(ccaps_map[bob_handle]))

    fixed, allowed = ccaps_map[bob_handle][0]

    assertSameSets({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
                    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT}, fixed)
    assertSameSets([cs.TARGET_HANDLE], allowed)

    # old Capabilities
    all_caps = conn.Capabilities.GetCapabilities([bob_handle])
    assertEquals(1, len(all_caps))

    print all_caps

    caps = all_caps[0]

    assertEquals((bob_handle, cs.CHANNEL_TYPE_TEXT, 3, 0), caps)

if __name__ == '__main__':
    exec_test(test)
