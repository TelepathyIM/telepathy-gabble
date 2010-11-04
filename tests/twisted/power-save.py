"""
Test entering and leaving power saving mode.
"""

import config

import constants as cs

from gabbletest import exec_test, GoogleXmlStream, make_result_iq, \
    send_error_reply, disconnect_conn
from servicetest import call_async, Event, assertEquals, EventPattern, \
    assertContains, assertDoesNotContain
import ns

import dbus
import dbus.service

from twisted.internet import reactor
from twisted.words.xish import domish

def expect_command(q, name):
    event = q.expect('stream-iq', query_name='query', query_ns=ns.GOOGLE_QUEUE)
    command = event.query.firstChildElement()
    assertEquals(name, command.name)
    return event.stanza

def test_error(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])


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

def test_no_support(q, bus, conn, stream):
    assertDoesNotContain(cs.CONN_IFACE_POWER_SAVING,
                         conn.Get(cs.CONN, "Interfaces",
                                  dbus_interface=cs.PROPERTIES_IFACE))

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    assertDoesNotContain(cs.CONN_IFACE_POWER_SAVING,
                         conn.Get(cs.CONN, "Interfaces",
                                  dbus_interface=cs.PROPERTIES_IFACE))

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

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


def test_on_connect_error(q, bus, conn, stream):
    pattern = [EventPattern('dbus-signal', signal='PowerSavingChanged',
                            args=[True])]

    q.forbid_events(pattern)

    call_async(q, conn.PowerSaving, 'SetPowerSaving', True)

    q.expect('dbus-error', method='SetPowerSaving', name=cs.DISCONNECTED)

    q.unforbid_events(pattern)

def test_disconnect(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

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
    exec_test(test_no_support)
    exec_test(test_error, protocol=GoogleXmlStream)
    exec_test(test_on_connect_error)
    exec_test(test_disconnect, protocol=GoogleXmlStream)
