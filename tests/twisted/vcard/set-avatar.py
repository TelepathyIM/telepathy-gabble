"""
Tests the very simple case of "setting your own avatar".
"""

from twisted.words.xish import xpath
from servicetest import call_async, assertEquals
from gabbletest import (
    exec_test, expect_and_handle_get_vcard, expect_and_handle_set_vcard,
    )
import base64
import functools

def test(q, bus, conn, stream, image_data, mime_type):
    call_async(q, conn.Avatars, 'SetAvatar', image_data, mime_type)

    expect_and_handle_get_vcard(q, stream)

    def check(vcard):
        assertEquals(mime_type,
            xpath.queryForString('/vCard/PHOTO/TYPE', vcard))

        binval = xpath.queryForString('/vCard/PHOTO/BINVAL', vcard)

        # <http://xmpp.org/extensions/xep-0153.html#bizrules-image> says:
        #
        #  5. The image data MUST conform to the base64Binary datatype and thus
        #     be encoded in accordance with Section 6.8 of RFC 2045, which
        #     recommends that base64 data should have lines limited to at most
        #     76 characters in length.
        lines = binval.split('\n')
        for line in lines:
            assert len(line) <= 76, line

        assertEquals(image_data, base64.decodestring(binval))

    expect_and_handle_set_vcard(q, stream, check=check)

    q.expect('dbus-return', method='SetAvatar')

def test_little_avatar(q, bus, conn, stream):
    test(q, bus, conn, stream, image_data='Guy.brush',
        mime_type='image/x-mighty-pirate')

def test_massive_avatar(q, bus, conn, stream):
    """Regression test for
    <https://bugs.freedesktop.org/show_bug.cgi?id=57080>, where a too-small
    buffer was allocated if the base64-encoded avatar spanned multiple lines.

    """
    lol = 'lo' * (4 * 1024)
    test(q, bus, conn, stream, image_data=lol, mime_type='image/x-lolololo')

if __name__ == '__main__':
    exec_test(test_little_avatar)
    exec_test(test_massive_avatar)
