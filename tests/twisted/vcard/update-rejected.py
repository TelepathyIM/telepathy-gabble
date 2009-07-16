"""
Regression test for fd.o #20442, where the XMPP error returned by a server that
doesn't like the avatar you tried to set was not mapped to a TP error before
being sent over the bus.
"""

from twisted.words.xish import domish

from servicetest import call_async
from gabbletest import exec_test, expect_and_handle_get_vcard, send_error_reply, sync_stream

import ns
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()

    expect_and_handle_get_vcard(q, stream)
    sync_stream(q, stream)

    call_async(q, conn.Avatars, 'SetAvatar', 'william shatner',
        'image/x-actor-name')
    # Gabble request the last version of the vCard before changing it
    expect_and_handle_get_vcard(q, stream)

    set_vcard_event = q.expect('stream-iq', query_ns=ns.VCARD_TEMP,
        query_name='vCard', iq_type='set')
    iq = set_vcard_event.stanza

    error = domish.Element((None, 'error'))
    error['code'] = '400'
    error['type'] = 'modify'
    error.addElement((ns.STANZA, 'bad-request'))

    send_error_reply(stream, iq, error)

    event = q.expect('dbus-error', method='SetAvatar')

    assert event.error.get_dbus_name() == cs.INVALID_ARGUMENT, \
        event.error.get_dbus_name()

if __name__ == '__main__':
    exec_test(test)
