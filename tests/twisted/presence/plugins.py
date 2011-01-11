"""
Test Gabble support for plugins adding presence statuses.
"""
from gabbletest import (
    XmppXmlStream, exec_test, make_presence, elem, elem_iq,
    acknowledge_iq, send_error_reply
)
from servicetest import (
    EventPattern, assertEquals, assertNotEquals, assertContains,
    assertDoesNotContain, call_async
)
import ns
import constants as cs

from twisted.words.xish import xpath, domish

from invisible_helper import Xep0186AndManualPrivacyListStream, \
    ManualPrivacyListStream

def test(q, bus, conn, stream):
    statuses = conn.Properties.Get(cs.CONN_IFACE_SIMPLE_PRESENCE,
        'Statuses')

    # testbusy and testaway are provided by test plugin
    assertContains('testbusy', statuses)
    assertContains('testaway', statuses)

    assertEquals(statuses['testbusy'][0], cs.PRESENCE_BUSY)
    assertEquals(statuses['testaway'][0], cs.PRESENCE_AWAY)

    conn.SimplePresence.SetPresence('testbusy', '')

    conn.Connect()

    # ... gabble asks for all the available lists on the server ...
    stream.handle_get_all_privacy_lists(q, bus, conn,
        lists=["foo-list", "test-busy-list", "bar-list"])

    # ... gabble checks whether there's usable invisible list on the server ...
    get_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='get')
    list_node = xpath.queryForNodes('//list', get_list.query)[0]
    assertEquals('invisible', list_node['name'])

    error = domish.Element((None, 'error'))
    error['type'] = 'cancel'
    error.addElement((ns.STANZA, 'item-not-found'))
    send_error_reply (stream, get_list.stanza, error)

    # ... since there is none, Gabble creates it ...
    create_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    list_node = xpath.queryForNodes('//list', create_list.query)[0]
    assertEquals('invisible', list_node['name'])
    assertNotEquals([],
        xpath.queryForNodes('/query/list/item/presence-out',
            create_list.query))
    acknowledge_iq(stream, create_list.stanza)

    get_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    list_node = xpath.queryForNodes('//active', get_list.query)[0]

    # ... and then activates the one linked with the requested status
    # Note: testbusy status is linked to test-busy-list by test plugin
    assertEquals('test-busy-list', list_node['name'])
    acknowledge_iq(stream, get_list.stanza)

    q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{1L: (0L, {u'testbusy': {}})}])
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # ... testaway is not supposed to be settable on us
    call_async(q, conn.SimplePresence, 'SetPresence', 'testaway', '')
    q.expect('dbus-error', method='SetPresence', name=cs.INVALID_ARGUMENT)

def test_with_xep0186(q, bus, conn, stream):
    statuses = conn.Properties.Get(cs.CONN_IFACE_SIMPLE_PRESENCE,
        'Statuses')

    # testbusy and testaway are provided by test plugin
    assertContains('testbusy', statuses)
    assertContains('testaway', statuses)

    assertEquals(statuses['testbusy'][0], cs.PRESENCE_BUSY)
    assertEquals(statuses['testaway'][0], cs.PRESENCE_AWAY)

    conn.SimplePresence.SetPresence('testbusy', '')

    conn.Connect()

    # ... gabble asks for all the available lists on the server ...
    stream.handle_get_all_privacy_lists(q, bus, conn,
        lists=["foo-list", "test-busy-list", "bar-list"])

    get_list = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='set')
    list_node = xpath.queryForNodes('//active', get_list.query)[0]

    # ... and then activates the one linked with the requested status
    # Note: testbusy status is linked to test-busy-list by test plugin
    assertEquals('test-busy-list', list_node['name'])
    acknowledge_iq(stream, get_list.stanza)

    q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{1L: (0L, {u'testbusy': {}})}])
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # ... testaway is not supposed to be settable on us
    call_async(q, conn.SimplePresence, 'SetPresence', 'testaway', '')
    q.expect('dbus-error', method='SetPresence', name=cs.INVALID_ARGUMENT)


if __name__ == '__main__':
    exec_test(test, protocol=ManualPrivacyListStream, do_connect=False)
    exec_test(test_with_xep0186, protocol=Xep0186AndManualPrivacyListStream, do_connect=False)

