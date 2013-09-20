"""
A simple smoke-test for C.I.SimplePresence
"""

from twisted.words.xish import domish

from gabbletest import exec_test, make_presence
from servicetest import EventPattern, assertEquals
import ns
import constants as cs

def test(q, bus, conn, stream):
    event = q.expect('stream-iq', query_ns=ns.ROSTER)

    amy_handle = conn.get_contact_handle_sync('amy@foo.com')

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

    e = q.expect('dbus-signal', signal='PresencesChanged',
        args=[{amy_handle:
            (cs.PRESENCE_AVAILABLE, 'chat', 'I may have been drinking')}])

    amy_handle, asv = conn.Contacts.GetContactByID('amy@foo.com',
            [cs.CONN_IFACE_SIMPLE_PRESENCE])
    assertEquals(e.args[0][amy_handle], asv.get(cs.ATTR_PRESENCE))

    bob_handle, asv = conn.Contacts.GetContactByID('bob@foo.com',
            [cs.CONN_IFACE_SIMPLE_PRESENCE])
    assertEquals((cs.PRESENCE_UNKNOWN, 'unknown', ''),
            asv.get(cs.ATTR_PRESENCE))

if __name__ == '__main__':
    exec_test(test)
