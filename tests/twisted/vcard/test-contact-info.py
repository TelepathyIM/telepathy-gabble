
"""
Test ContactInfo support.
"""

from servicetest import call_async, EventPattern
from gabbletest import exec_test, acknowledge_iq, make_result_iq
import constants as cs
import dbus


def test(q, bus, conn, stream):
    conn.Connect()
    _, event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, event.stanza)

    handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
    call_async(q, conn.ContactInfo, 'GetContactInfo', [handle])

    event = q.expect('stream-iq', to='bob@foo.com', query_ns='vcard-temp',
        query_name='vCard')
    result = make_result_iq(stream, event.stanza)
    result.firstChildElement().addElement('FN', content='Bob')
    n = result.firstChildElement().addElement('N')
    n.addElement('GIVEN', content='Bob')
    result.firstChildElement().addElement('NICKNAME',
        content=r'bob,bob1\,,bob2,bob3\,bob4')
    label = result.firstChildElement().addElement('LABEL')
    label.addElement('LINE', content='42 West Wallaby Street')
    label.addElement('LINE', content="Bishop's Stortford\n")
    label.addElement('LINE', content='Huntingdon')
    stream.send(result)

    q.expect('dbus-signal', signal='ContactInfoChanged')

    # A second request should be satisfied from the cache.
    assert conn.ContactInfo.GetContactInfo([handle]) == \
        {handle: [(u'fn', [], [u'Bob']),
                  (u'n', [], [u'', u'Bob', u'', u'', u'']),
                  (u'nickname', [], [r'bob,bob1\,,bob2,bob3\,bob4']),
                  # LABEL comes out as a single blob of text
                  (u'label', [], ['42 West Wallaby Street\n'
                      "Bishop's Stortford\n"
                      'Huntingdon\n']),
                  ]}


if __name__ == '__main__':
    exec_test(test)
