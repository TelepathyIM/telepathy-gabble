"""
Tests the very simple case of "clearing your own avatar".
"""

from servicetest import call_async
from gabbletest import (
    exec_test, expect_and_handle_get_vcard, expect_and_handle_set_vcard, current_vcard
    )

def test(q, bus, conn, stream):
    photo = current_vcard.addElement((None, 'PHOTO'))
    photo.addElement((None, 'TYPE')).addContent('image/fake')
    photo.addElement((None, 'BINVAL')).addContent('NYANYANYANYANYAN')

    call_async(q, conn.Avatars, 'ClearAvatar')

    expect_and_handle_get_vcard(q, stream)

    def check(vcard):
        assert len(vcard.children) == 0, vcard.toXml()

    expect_and_handle_set_vcard(q, stream, check=check)

    q.expect('dbus-return', method='ClearAvatar')

if __name__ == '__main__':
    exec_test(test)
