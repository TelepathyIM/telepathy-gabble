"""
Test entering and leaving power saving mode.
"""

import config

import constants as cs

from gabbletest import exec_test, GoogleXmlStream, make_result_iq, \
    send_error_reply, disconnect_conn, make_presence, sync_stream, elem, \
    acknowledge_iq
from servicetest import call_async, Event, assertEquals, EventPattern, \
    assertContains, assertDoesNotContain, sync_dbus
import ns

import dbus
import dbus.service

from twisted.internet import reactor
from twisted.words.xish import domish

def expect_command(q, name):
    event = q.expect('stream-iq', query_name='query', query_ns=ns.GOOGLE_QUEUE)

    # Regression test: when we split MCE out of Gabble and moved Gabble's code
    # to conn-power-saving, this erroneously became a 'get'.
    assertEquals('set', event.iq_type)

    command = event.query.firstChildElement()
    assertEquals(name, command.name)
    return event.stanza

def test_error(q, bus, conn, stream):
    assertContains(cs.CONN_IFACE_POWER_SAVING,
                  conn.Get(cs.CONN, "Interfaces",
                           dbus_interface=cs.PROPERTIES_IFACE))

    assertEquals (False, conn.Get(cs.CONN_IFACE_POWER_SAVING,
                                  "PowerSavingActive",
                                  dbus_interface=cs.PROPERTIES_IFACE))

    call_async(q, conn.PowerSaving, 'SetPowerSaving', True)

    stanza = expect_command(q, 'enable')

    error = domish.Element((None, 'error'))

    error.addElement((ns.STANZA, 'service-unavailable'))

    send_error_reply(stream, stanza, error)

    q.expect('dbus-error', method='SetPowerSaving', name=cs.NOT_AVAILABLE)

    # Power saving state should remain false
    assertEquals (False, conn.Get(cs.CONN_IFACE_POWER_SAVING,
                                  "PowerSavingActive",
                                  dbus_interface=cs.PROPERTIES_IFACE))


def test_local_queueing(q, bus, conn, stream):
    assertContains(cs.CONN_IFACE_POWER_SAVING,
                  conn.Get(cs.CONN, "Interfaces",
                           dbus_interface=cs.PROPERTIES_IFACE))

    assertEquals (False, conn.Get(cs.CONN_IFACE_POWER_SAVING,
                                  "PowerSavingActive",
                                  dbus_interface=cs.PROPERTIES_IFACE))

    event = q.expect('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard')
    acknowledge_iq(stream, event.stanza)

    presence_update = [EventPattern('dbus-signal', signal='PresenceUpdate')]
    q.forbid_events(presence_update)

    call_async(q, conn.PowerSaving, 'SetPowerSaving', True)

    q.expect_many(EventPattern('dbus-return', method='SetPowerSaving'),
                  EventPattern('dbus-signal', signal='PowerSavingChanged',
                               args=[True]))

    assertEquals (True, conn.Get(cs.CONN_IFACE_POWER_SAVING,
                                  "PowerSavingActive",
                                  dbus_interface=cs.PROPERTIES_IFACE))

    # These presence stanzas should be queued
    stream.send(make_presence('amy@foo.com', show='away',
                              status='At the pub'))
    stream.send(make_presence('bob@foo.com', show='xa',
                              status='Somewhere over the rainbow'))

    # Pep notifications too
    message = elem('message', from_='bob@foo.com')(
        elem((ns.PUBSUB_EVENT), 'event')(
            elem('items', node=ns.NICK)(
                elem('item')(
                    elem(ns.NICK, 'nick')(u'Robert')
                )
            )
        )
    )
    stream.send(message.toXml())

    sync_dbus(bus, q, conn)
    q.unforbid_events(presence_update)

    # Incoming important stanza will flush the queue
    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com/Pidgin'
    m['id'] = '123'
    m['type'] = 'chat'
    m.addElement('body', content='important message')
    stream.send(m)

    # Presence updates should come in the original order ...
    p1 = q.expect('dbus-signal', signal='PresencesChanged')
    p2 = q.expect('dbus-signal', signal='PresencesChanged')

    assertEquals('away', p1.args[0].values()[0][1])
    assertEquals('xa', p2.args[0].values()[0][1])

    # .. followed by the result of PEP notification ..
    event = q.expect('dbus-signal', signal='AliasesChanged')

    # .. and finally the message that flushed the stanza queue
    q.expect('dbus-signal', signal='NewChannel')

    sync_stream(q, stream)

    q.forbid_events(presence_update)

    stream.send(make_presence('carl@foo.com', show='away',
                              status='Home'))

    # Carl's presence update is queued
    sync_dbus(bus, q, conn)
    q.unforbid_events(presence_update)

    # Disable powersaving, flushing the queue
    conn.PowerSaving.SetPowerSaving(False)

    q.expect('dbus-signal', signal='PresenceUpdate')


def test(q, bus, conn, stream):
    assertContains(cs.CONN_IFACE_POWER_SAVING,
                  conn.Get(cs.CONN, "Interfaces",
                           dbus_interface=cs.PROPERTIES_IFACE))

    assertEquals (False, conn.Get(cs.CONN_IFACE_POWER_SAVING,
                                  "PowerSavingActive",
                                  dbus_interface=cs.PROPERTIES_IFACE))

    call_async(q, conn.PowerSaving, 'SetPowerSaving', True)

    stanza = expect_command(q, 'enable')

    stream.send(make_result_iq(stream, stanza, False))

    q.expect_many(EventPattern('dbus-return', method='SetPowerSaving'),
                  EventPattern('dbus-signal', signal='PowerSavingChanged',
                               args=[True]))

    assertEquals (True, conn.Get(cs.CONN_IFACE_POWER_SAVING,
                                  "PowerSavingActive",
                                  dbus_interface=cs.PROPERTIES_IFACE))

    pattern = [EventPattern('stream-iq', query_name='query',
                            query_ns=ns.GOOGLE_QUEUE),
               EventPattern('dbus-signal', signal='PowerSavingChanged',
                            args=[True])]

    q.forbid_events(pattern)

    # Enabling power saving again should be a no-op.
    call_async(q, conn.PowerSaving, 'SetPowerSaving', True)

    q.expect('dbus-return', method='SetPowerSaving')

    q.unforbid_events(pattern)

    call_async(q, conn.PowerSaving, 'SetPowerSaving', False)

    stanza = expect_command(q, 'disable')

    stream.send(make_result_iq(stream, stanza, False))

    event, _, _ = q.expect_many(
        EventPattern('stream-iq', query_name='query', query_ns=ns.GOOGLE_QUEUE),
        EventPattern('dbus-return', method='SetPowerSaving'),
        EventPattern('dbus-signal', signal='PowerSavingChanged',
                     args=[False]))

    command = event.query.firstChildElement()
    assertEquals("flush", command.name)

    assertEquals (False, conn.Get(cs.CONN_IFACE_POWER_SAVING,
                                  "PowerSavingActive",
                                  dbus_interface=cs.PROPERTIES_IFACE))


def test_disconnect(q, bus, conn, stream):
    assertContains(cs.CONN_IFACE_POWER_SAVING,
                  conn.Get(cs.CONN, "Interfaces",
                           dbus_interface=cs.PROPERTIES_IFACE))

    assertEquals (False, conn.Get(cs.CONN_IFACE_POWER_SAVING,
                                  "PowerSavingActive",
                                  dbus_interface=cs.PROPERTIES_IFACE))

    call_async(q, conn.PowerSaving, 'SetPowerSaving', True)

    stanza = expect_command(q, 'enable')

    disconnect_conn(q, conn, stream)

if __name__ == '__main__':
    exec_test(test, protocol=GoogleXmlStream)
    exec_test(test_local_queueing)
    exec_test(test_error, protocol=GoogleXmlStream)
    exec_test(test_disconnect, protocol=GoogleXmlStream)
