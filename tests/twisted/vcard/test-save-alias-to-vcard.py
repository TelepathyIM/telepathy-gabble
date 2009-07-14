"""
Regression test.

 - the 'alias' connection parameter is set
 - our vCard doesn't have a NICKNAME field
 - we crash when trying to save a vcard with NICKNAME set to the alias
   parameter
"""

from gabbletest import (
    exec_test, expect_and_handle_get_vcard, expect_and_handle_set_vcard)

def test(q, bus, conn, stream):
    conn.Connect()

    expect_and_handle_get_vcard(q, stream)

    def check_vcard(vcard):
        for e in vcard.elements():
            if e.name == 'NICKNAME':
                assert str(e) == 'Some Guy', e.toXml()
                return
        assert False, vcard.toXml()

    expect_and_handle_set_vcard(q, stream, check_vcard)

if __name__ == '__main__':
    exec_test(test, params={'alias': 'Some Guy'})
