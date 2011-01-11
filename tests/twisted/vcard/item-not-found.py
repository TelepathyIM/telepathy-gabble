"""
Regression test for <http://bugs.freedesktop.org/show_bug.cgi?id=25987> where
Gabble didn't understand that <item-not-found/> just means the user doesn't
have a vCard yet, and so it can go right ahead and set one.
"""

from twisted.words.xish import domish
from servicetest import call_async
from gabbletest import (
    exec_test, sync_stream, send_error_reply, expect_and_handle_set_vcard,
    )
import ns

def expect_get_and_send_item_not_found(q, stream):
    get_vcard_event = q.expect('stream-iq', query_ns=ns.VCARD_TEMP,
        query_name='vCard', iq_type='get')

    error = domish.Element((None, 'error'))
    error['type'] = 'cancel'
    error.addElement((ns.STANZA, 'item-not-found'))
    send_error_reply(stream, get_vcard_event.stanza, error)

def test(q, bus, conn, stream):
    expect_get_and_send_item_not_found(q, stream)

    sync_stream(q, stream)

    call_async(
        q, conn.Avatars, 'SetAvatar', 'Guy.brush', 'image/x-mighty-pirate')

    # Gabble checks again, but we still don't have a vCard
    expect_get_and_send_item_not_found(q, stream)

    # Never mind! It creates a new one.
    expect_and_handle_set_vcard(q, stream)

    q.expect('dbus-return', method='SetAvatar')

if __name__ == '__main__':
    exec_test(test)
