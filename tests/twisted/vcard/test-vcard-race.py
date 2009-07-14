"""
Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=11201

 - We try to set our own alias and our own avatar at about the same time
 - SetAliases requests vCard v1
 - SetAvatar requests vCard v1
 - SetAliases receives v1, writes back v2 with new NICKNAME
 - SetAvatar receives v1, writes back v2' with new PHOTO
 - Change to NICKNAME in v2 is lost
"""

print "FIXME: test-vcard-race.py disabled because it's racy"
print "       http://bugs.freedesktop.org/show_bug.cgi?id=22023"
raise SystemExit(77)

import base64

from twisted.words.xish import xpath

from servicetest import call_async
from gabbletest import (
    exec_test, expect_and_handle_get_vcard, expect_and_handle_set_vcard)

def test(q, bus, conn, stream):
    conn.Connect()

    expect_and_handle_get_vcard(q, stream)

    call_async(q, conn, 'GetSelfHandle')
    event = q.expect('dbus-return', method='GetSelfHandle')
    handle = event.value[0]

    call_async(q, conn.Aliasing, 'SetAliases', {handle: 'Some Guy'})
    call_async(q, conn.Avatars, 'SetAvatar', 'hello', 'image/png')

    # Gabble asks for the self-vCard again (FIXME: why? Interestingly, when I
    # called GetSelfHandle synchronously Gabble *didn't* ask for the vCard
    # again.)
    expect_and_handle_get_vcard(q, stream)

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
