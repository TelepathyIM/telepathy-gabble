
"""
Test avatar support with multiple resources as defined in XEP-0153 section
4.3.
"""

import base64

from twisted.words.xish import domish
from servicetest import call_async, EventPattern, sync_dbus
from gabbletest import exec_test, acknowledge_iq, make_result_iq, sync_stream
import constants as cs
import ns

def make_presence(jid, sha1sum):
    p = domish.Element((None, 'presence'))
    p['from'] = jid
    x = p.addElement((ns.VCARD_TEMP_UPDATE, 'x'))
    x.addElement('photo', content=sha1sum)
    return p

def test(q, bus, conn, stream):
    conn.Connect()
    _, event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, event.stanza)
    sync_stream(q, stream)
    sync_dbus(bus, q, conn)

    # A presence from a contact
    stream.send(make_presence('contact1@localhost/client',
        'SHA1SUM-FOR-CONTACT1'))
    event = q.expect('dbus-signal', signal='AvatarUpdated')
    assert event.args[0] == 2, event.args
    assert event.args[1] == "SHA1SUM-FOR-CONTACT1", event.args

    AvatarRetrieved_event = EventPattern('dbus-signal', signal='AvatarRetrieved')
    AvatarUpdated_event = EventPattern('dbus-signal', signal='AvatarUpdated')
    StreamPresence_event = EventPattern('stream-presence')
    StreamIqVcard_event = EventPattern('stream-iq', query_ns='vcard-temp')

    # A presence from myself on another resource
    stream.send(make_presence('test@localhost/resource1',
        'SHA1SUM-FOR-MYSELF-RES1'))
    q.forbid_events([AvatarRetrieved_event, AvatarUpdated_event])
    stream_presence, stream_iq = q.expect_many(
        EventPattern('stream-presence'),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))
    sync_dbus(bus, q, conn)
    q.unforbid_events([AvatarRetrieved_event, AvatarUpdated_event])

    # If the server wrongly send a presence stanza with our resource,
    # AvatarUpdated must not be emitted
    q.forbid_events([StreamPresence_event, StreamIqVcard_event, AvatarRetrieved_event, AvatarUpdated_event])
    stream.send(make_presence('test@localhost/Resource',
        'SHA1SUM-FOR-MYSELF'))
    sync_dbus(bus, q, conn)
    sync_stream(q, stream)
    q.unforbid_events([StreamPresence_event, StreamIqVcard_event, AvatarRetrieved_event, AvatarUpdated_event])

if __name__ == '__main__':
    exec_test(test)
