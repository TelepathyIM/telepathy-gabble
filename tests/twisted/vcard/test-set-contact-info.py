
"""
Test ContactInfo setting support.
"""

from servicetest import (EventPattern, call_async, assertEquals, assertLength,
        assertContains)
from gabbletest import exec_test, acknowledge_iq
import constants as cs

from twisted.words.xish import xpath

def test(q, bus, conn, stream):
    conn.Connect()
    _, event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))
    # FIXME: this is racy: the vCard get sometimes happens before we want it to

    acknowledge_iq(stream, event.stanza)

    call_async(q, conn.ContactInfo, 'SetContactInfo',
               [(u'fn', [], [u'Wee Ninja']),
                (u'n', ['language=ja'], [u'Ninja', u'Wee', u'', u'', u'-san']),
                (u'org', [], ['Collabora, Ltd.',
                    'Human Resources; Company Policy Enforcement']),
                (u'adr', ['type=work','type=postal','type=parcel'],
                    ['', '', '11 Kings Parade', 'Cambridge', 'Cambridgeshire',
                        'CB2 1SJ', 'UK']),
                (u'tel', ['type=voice','type=work'], ['+44 1223 362967']),
                (u'tel', ['type=voice','type=work'], ['+44 7700 900753']),
                (u'email', ['type=internet','type=pref'],
                    ['wee.ninja@collabora.co.uk']),
                (u'email', ['type=internet'], ['wee.ninja@example.com']),
                (u'url', [], ['http://www.thinkgeek.com/geektoys/plush/8823/']),
                (u'nickname', [], [u'HR Ninja']),
                (u'nickname', [], [u'Enforcement Ninja'])])

    vcard_set_event = q.expect('stream-iq', iq_type='set',
                query_ns='vcard-temp', query_name='vCard')

    assertLength(2, xpath.queryForNodes('/iq/vCard/NICKNAME',
        vcard_set_event.stanza))
    nicknames = []
    for nickname in xpath.queryForNodes('/iq/vCard/NICKNAME',
            vcard_set_event.stanza):
        nicknames.append(str(nickname))
    assertEquals(['HR Ninja', 'Enforcement Ninja'], nicknames)

    assertEquals(None, xpath.queryForNodes('/iq/vCard/PHOTO',
        vcard_set_event.stanza))
    assertLength(1, xpath.queryForNodes('/iq/vCard/N',
        vcard_set_event.stanza))
    assertEquals('Wee', xpath.queryForString('/iq/vCard/N/GIVEN',
        vcard_set_event.stanza))
    assertEquals('Ninja', xpath.queryForString('/iq/vCard/N/FAMILY',
        vcard_set_event.stanza))
    assertEquals('-san', xpath.queryForString('/iq/vCard/N/SUFFIX',
        vcard_set_event.stanza))
    assertEquals('Wee Ninja', xpath.queryForString('/iq/vCard/FN',
        vcard_set_event.stanza))

    assertLength(1, xpath.queryForNodes('/iq/vCard/ORG',
        vcard_set_event.stanza))
    assertEquals('Collabora, Ltd.',
            xpath.queryForString('/iq/vCard/ORG/ORGNAME',
                vcard_set_event.stanza))
    assertEquals('Human Resources; Company Policy Enforcement',
            xpath.queryForString('/iq/vCard/ORG/ORGUNIT',
                vcard_set_event.stanza))

    assertLength(2, xpath.queryForNodes('/iq/vCard/TEL',
        vcard_set_event.stanza))
    for tel in xpath.queryForNodes('/iq/vCard/TEL', vcard_set_event.stanza):
        assertLength(1, xpath.queryForNodes('/TEL/NUMBER', tel))
        assertContains(xpath.queryForString('/TEL/NUMBER', tel),
                ('+44 1223 362967', '+44 7700 900753'))
        assertLength(1, xpath.queryForNodes('/TEL/VOICE', tel))
        assertLength(1, xpath.queryForNodes('/TEL/WORK', tel))

    assertLength(2, xpath.queryForNodes('/iq/vCard/EMAIL',
        vcard_set_event.stanza))
    for email in xpath.queryForNodes('/iq/vCard/EMAIL',
            vcard_set_event.stanza):
        assertContains(xpath.queryForString('/EMAIL/USERID', email),
                ('wee.ninja@example.com', 'wee.ninja@collabora.co.uk'))
        assertLength(1, xpath.queryForNodes('/EMAIL/INTERNET', email))
        if 'collabora' in xpath.queryForString('/EMAIL/USERID', email):
            assertLength(1, xpath.queryForNodes('/EMAIL/PREF', email))
        else:
            assertEquals(None, xpath.queryForNodes('/EMAIL/PREF', email))

    acknowledge_iq(stream, vcard_set_event.stanza)
    q.expect_many(
            EventPattern('dbus-return', method='SetContactInfo'),
            EventPattern('dbus-signal', signal='AliasesChanged',
                predicate=lambda e: e.args[0][0][1] == 'HR Ninja'),
            EventPattern('dbus-signal', signal='ContactInfoChanged'),
            # FIXME: check the args of ContactInfoChanged. We expect
            # language=ja to have been dropped (doesn't actually happen yet),
            # and the type=foo parameters might have been re-ordered
            )

    # Following a reshuffle, Company Policy Enforcement is declared to be
    # a sub-department within Human Resources, and the ninja no longer
    # qualifies for a company phone
    call_async(q, conn.ContactInfo, 'SetContactInfo',
               [(u'fn', [], [u'Wee Ninja']),
                (u'n', ['language=ja'], [u'Ninja', u'Wee', u'', u'', u'-san']),
                (u'org', [], ['Collabora, Ltd.',
                    'Human Resources', 'Company Policy Enforcement']),
                (u'adr', ['type=work','type=postal','type=parcel'],
                    ['', '', '11 Kings Parade', 'Cambridge', 'Cambridgeshire',
                        'CB2 1SJ', 'UK']),
                (u'tel', ['type=voice','type=work'], ['+44 1223 362967']),
                (u'email', ['type=internet','type=pref'],
                    ['wee.ninja@collabora.co.uk']),
                (u'email', ['type=internet'], ['wee.ninja@example.com']),
                (u'url', [], ['http://www.thinkgeek.com/geektoys/plush/8823/']),
                (u'nickname', [], [u'HR Ninja']),
                (u'nickname', [], [u'Enforcement Ninja'])])

    event = q.expect('stream-iq', iq_type='get', query_ns='vcard-temp',
        query_name='vCard')
    acknowledge_iq(stream, event.stanza)

    vcard_set_event = q.expect('stream-iq', iq_type='set',
            query_ns='vcard-temp', query_name='vCard')

    # FIXME: ORG is currently just omitted if you try to have more than one
    # ORGUNIT
    #assertLength(1, xpath.queryForNodes('/iq/vCard/ORG',
    #    vcard_set_event.stanza))
    #assertEquals('Collabora, Ltd.',
    #        xpath.queryForString('/iq/vCard/ORG/ORGNAME',
    #            vcard_set_event.stanza))
    #assertEquals('Human Resources; Company Policy Enforcement',
    #        xpath.queryForString('/iq/vCard/ORG/ORGUNIT',
    #            vcard_set_event.stanza))

    assertLength(1, xpath.queryForNodes('/iq/vCard/TEL',
        vcard_set_event.stanza))
    for tel in xpath.queryForNodes('/iq/vCard/TEL', vcard_set_event.stanza):
        assertLength(1, xpath.queryForNodes('/TEL/NUMBER', tel))
        assertEquals('+44 1223 362967',
                xpath.queryForString('/TEL/NUMBER', tel))
        assertLength(1, xpath.queryForNodes('/TEL/VOICE', tel))
        assertLength(1, xpath.queryForNodes('/TEL/WORK', tel))

    acknowledge_iq(stream, vcard_set_event.stanza)
    q.expect_many(
            EventPattern('dbus-return', method='SetContactInfo'),
            EventPattern('dbus-signal', signal='ContactInfoChanged'),
            )

    # Finally, the ninja decides that publishing his contact details is not
    # very ninja-like, and decides to be anonymous.
    call_async(q, conn.ContactInfo, 'SetContactInfo', [])

    event = q.expect('stream-iq', iq_type='get', query_ns='vcard-temp',
        query_name='vCard')
    acknowledge_iq(stream, event.stanza)

    vcard_set_event = q.expect('stream-iq', iq_type='set',
            query_ns='vcard-temp', query_name='vCard')
    assertEquals(None, xpath.queryForNodes('/iq/vCard/*',
        vcard_set_event.stanza))

    acknowledge_iq(stream, vcard_set_event.stanza)
    q.expect_many(
            EventPattern('dbus-return', method='SetContactInfo'),
            EventPattern('dbus-signal', signal='ContactInfoChanged'),
            )

if __name__ == '__main__':
    exec_test(test)
