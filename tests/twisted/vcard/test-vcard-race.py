"""
Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=11201

 - We try to set our own alias and our own avatar at about the same time
 - SetAliases requests vCard v1
 - SetAvatar requests vCard v1
 - SetAliases receives v1, writes back v2 with new NICKNAME
 - SetAvatar receives v1, writes back v2' with new PHOTO
 - Change to NICKNAME in v2 is lost
"""

import base64

from twisted.words.xish import xpath

from servicetest import call_async, sync_dbus
from gabbletest import (
    exec_test, expect_and_handle_get_vcard, expect_and_handle_set_vcard,
    make_result_iq, sync_stream)
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    expect_and_handle_get_vcard(q, stream)

    # Ensure that Gabble's actually got the initial vCard reply; if it hasn't
    # processed it by the time we call SetAliases, the latter will wait for it
    # to reply and then set immediately.
    sync_stream(q, stream)

    handle = conn.GetSelfHandle()

    call_async(q, conn.Aliasing, 'SetAliases', {handle: 'Some Guy'})

    # SetAliases requests vCard v1
    get_vcard_event = q.expect('stream-iq', query_ns=ns.VCARD_TEMP,
        query_name='vCard', iq_type='get')

    iq = get_vcard_event.stanza
    vcard = iq.firstChildElement()
    assert vcard.name == 'vCard', vcard.toXml()

    call_async(q, conn.Avatars, 'SetAvatar', 'hello', 'image/png')

    # We don't expect Gabble to send a second vCard request, since there's one
    # outstanding. But we want to ensure that SetAvatar reaches Gabble before
    # the empty vCard does.
    sync_dbus(bus, q, conn)

    # Send back current empty vCard
    result = make_result_iq(stream, iq)
    # result already includes the <vCard/> from the query, which is all we need
    stream.send(result)

    def has_nickname_and_photo(vcard):
        nicknames = xpath.queryForNodes('/vCard/NICKNAME', vcard)
        assert nicknames is not None
        assert len(nicknames) == 1
        assert str(nicknames[0]) == 'Some Guy'

        photos = xpath.queryForNodes('/vCard/PHOTO', vcard)
        assert photos is not None and len(photos) == 1, repr(photos)
        types = xpath.queryForNodes('/PHOTO/TYPE', photos[0])
        binvals = xpath.queryForNodes('/PHOTO/BINVAL', photos[0])
        assert types is not None and len(types) == 1, repr(types)
        assert binvals is not None and len(binvals) == 1, repr(binvals)
        assert str(types[0]) == 'image/png'
        got = str(binvals[0])
        exp = base64.b64encode('hello')
        assert got == exp, (got, exp)

    # Now Gabble should set a new vCard with both of the above changes.
    expect_and_handle_set_vcard(q, stream, has_nickname_and_photo)

if __name__ == '__main__':
    exec_test(test)
