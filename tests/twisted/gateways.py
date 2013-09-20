"""
Test the gateways plugin
"""

import dbus
from twisted.words.xish import domish, xpath

from servicetest import (
    sync_dbus, call_async, EventPattern, assertEquals, assertContains,
    )
from gabbletest import (
    exec_test, send_error_reply, acknowledge_iq, sync_stream,
    make_presence,
    )
import constants as cs
import ns
from config import PLUGINS_ENABLED

PLUGIN_IFACE = cs.PREFIX + ".Gabble.Plugin.Gateways"

if not PLUGINS_ENABLED:
    print "NOTE: built without --enable-plugins, not testing plugins"
    raise SystemExit(77)

def test_success(q, gateways_iface, stream):
    call_async(q, gateways_iface, 'Register',
            'talkd.example.com', '1970', 's3kr1t')
    e = q.expect('stream-iq', iq_type='set', query_name='query',
            query_ns=ns.REGISTER, to='talkd.example.com')
    assertEquals('1970', xpath.queryForString('/query/username', e.query))
    assertEquals('s3kr1t', xpath.queryForString('/query/password', e.query))
    acknowledge_iq(stream, e.stanza)
    q.expect_many(
            EventPattern('dbus-return', method='Register'),
            EventPattern('stream-presence', presence_type='subscribe',
                to='talkd.example.com'),
            )
    stream.send(make_presence('talkd.example.com', type='subscribed'))

def test_conflict(q, gateways_iface, stream):
    call_async(q, gateways_iface, 'Register',
            'sip.example.com', '8675309', 'jenny')
    e = q.expect('stream-iq', iq_type='set', query_name='query',
            query_ns=ns.REGISTER, to='sip.example.com')
    assertEquals('8675309', xpath.queryForString('/query/username', e.query))
    assertEquals('jenny', xpath.queryForString('/query/password', e.query))
    error = domish.Element((None, 'error'))
    error['type'] = 'cancel'
    error['code'] = '409'
    error.addElement((ns.STANZA, 'conflict'))
    send_error_reply(stream, e.stanza, error)
    q.expect('dbus-error', method='Register', name=cs.REGISTRATION_EXISTS)

def test_not_acceptable(q, gateways_iface, stream):
    call_async(q, gateways_iface, 'Register',
            'fully-captcha-enabled.example.com', 'lalala', 'stoats')
    e = q.expect('stream-iq', iq_type='set', query_name='query',
            query_ns=ns.REGISTER, to='fully-captcha-enabled.example.com')
    assertEquals('lalala', xpath.queryForString('/query/username', e.query))
    assertEquals('stoats', xpath.queryForString('/query/password', e.query))
    error = domish.Element((None, 'error'))
    error['type'] = 'modify'
    error['code'] = '406'
    error.addElement((ns.STANZA, 'not-acceptable'))
    send_error_reply(stream, e.stanza, error)
    q.expect('dbus-error', method='Register', name=cs.NOT_AVAILABLE)

def test(q, bus, conn, stream):
    # Request a sidecar thate we support before we're connected; it should just
    # wait around until we're connected.
    call_async(q, conn.Future, 'EnsureSidecar', PLUGIN_IFACE)

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # Now we're connected, the call we made earlier should return.
    path, props = q.expect('dbus-return', method='EnsureSidecar').value
    # This sidecar doesn't even implement get_immutable_properties; it
    # should just get the empty dict filled in for it.
    assertEquals({}, props)

    gateways_iface = dbus.Interface(bus.get_object(conn.bus_name, path),
            PLUGIN_IFACE)

    test_success(q, gateways_iface, stream)
    test_conflict(q, gateways_iface, stream)
    test_not_acceptable(q, gateways_iface, stream)

    call_async(q, conn, 'Disconnect')

    q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-closed'),
        )

    stream.sendFooter()
    q.expect('dbus-return', method='Disconnect')

if __name__ == '__main__':
    exec_test(test, do_connect=False)
