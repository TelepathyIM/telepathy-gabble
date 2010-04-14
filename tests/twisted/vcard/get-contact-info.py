
"""
Test ContactInfo support.
"""

from servicetest import call_async, EventPattern, assertEquals
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
    call_async(q, conn.ContactInfo, 'RefreshContactInfo', [handle])

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
    org = result.firstChildElement().addElement('ORG')
    # ORG is a sequence of decreasingly large org.units, starting
    # with the organisation name itself (but here we've moved the org name
    # to the end, to make sure that works.)
    org.addElement('ORGUNIT', content='Dept. of Examples')
    org.addElement('ORGUNIT', content='Exemplary Team')
    org.addElement('ORGNAME', content='Collabora Ltd.')
    stream.send(result)

    q.expect('dbus-signal', signal='ContactInfoChanged')

    # The request should be satisfied from the cache.
    assertEquals(
        {handle: [(u'fn', [], [u'Bob']),
                  (u'n', [], [u'', u'Bob', u'', u'', u'']),
                  (u'nickname', [], [r'bob,bob1\,,bob2,bob3\,bob4']),
                  # LABEL comes out as a single blob of text
                  (u'label', [], ['42 West Wallaby Street\n'
                      "Bishop's Stortford\n"
                      'Huntingdon\n']),
                  # ORG is a sequence of decreasingly large org.units, starting
                  # with the organisation
                  (u'org', [], [u'Collabora Ltd.', u'Dept. of Examples',
                      u'Exemplary Team']),
                  ]}, conn.ContactInfo.GetContactInfo([handle]))


if __name__ == '__main__':
    exec_test(test)
