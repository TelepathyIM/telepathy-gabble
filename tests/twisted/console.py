"""
A smoketest for the XMPP console API.
"""

from servicetest import (
    ProxyWrapper, EventPattern,
    call_async, assertEquals, assertNotEquals, assertContains, sync_dbus,
)
from gabbletest import exec_test, acknowledge_iq, elem, elem_iq
from config import PLUGINS_ENABLED
import ns
import constants as cs

CONSOLE_PLUGIN_IFACE = cs.PREFIX + ".Gabble.Plugin.Console"
STACY = 'stacy@pilgrim.lit'

if not PLUGINS_ENABLED:
    print("NOTE: built without --enable-plugins, not testing XMPP console")
    raise SystemExit(77)

def send_unrecognised_get(q, stream):
    stream.send(
        elem_iq(stream, 'get')(
          elem('urn:unimaginative', 'dont-handle-me-bro')
        ))

    return q.expect('stream-iq', iq_type='error')

def test(q, bus, conn, stream):
    rccs = conn.Properties.Get(cs.CONN_IFACE_REQUESTS,
        'RequestableChannelClasses')

    fixed = {
        cs.CHANNEL_TYPE: CONSOLE_PLUGIN_IFACE,
        cs.TARGET_HANDLE_TYPE: cs.HT_NONE,
    }
    allowed = []
    assertContains((fixed, allowed), rccs)

    path, _ = conn.Requests.CreateChannel({ cs.CHANNEL_TYPE: CONSOLE_PLUGIN_IFACE })
    other_path, _ = conn.Requests.CreateChannel({ cs.CHANNEL_TYPE: CONSOLE_PLUGIN_IFACE })

    assertNotEquals(path, other_path)
    # leave the other one open, to test we don't crash on disconnect

    console = ProxyWrapper(bus.get_object(conn.bus_name, path),
        CONSOLE_PLUGIN_IFACE,
        {'Channel': cs.CHANNEL})

    assert not console.Properties.Get(CONSOLE_PLUGIN_IFACE, 'SpewStanzas')
    es = [
        EventPattern('dbus-signal', signal='StanzaReceived'),
        EventPattern('dbus-signal', signal='StanzaSent'),
        ]
    q.forbid_events(es)

    call_async(q, console, 'SendIQ', 'get', STACY,
        '<coffee xmlns="urn:unimaginative"/>')
    e = q.expect('stream-iq', iq_type='get', query_ns='urn:unimaginative',
        query_name='coffee')
    acknowledge_iq(stream, e.stanza)
    e = q.expect('dbus-return', method='SendIQ')
    type_, body = e.value
    assertEquals('result', type_)
    # We just assume the body works.

    # Turn on signalling incoming and outgoing stanzas
    console.Properties.Set(CONSOLE_PLUGIN_IFACE, 'SpewStanzas', True)
    sync_dbus(bus, q, conn)
    q.unforbid_events(es)

    send_unrecognised_get(q, stream)

    e = q.expect('dbus-signal', signal='StanzaReceived')
    xml, = e.args
    assertContains('<iq', xml)
    assertContains('<dont-handle-me-bro', xml)

    signal = q.expect('dbus-signal', signal='StanzaSent')
    assertContains('service-unavailable', signal.args[0])

    # Turn off spewing out stanzas; check it works.
    console.Properties.Set(CONSOLE_PLUGIN_IFACE, 'SpewStanzas', False)
    q.forbid_events(es)
    send_unrecognised_get(q, stream)
    sync_dbus(bus, q, conn)

    # Try sending just any old stanza
    console.SendStanza('''
        <message to='%(stacy)s' type='headline'>
          <body>
            Hi sis.
          </body>
        </message>''' % { 'stacy': STACY })

    e = q.expect('stream-message', to=STACY, message_type='headline')

    # Make sure that Wocky has filled in the jabber:client namespace we
    # carelessly omitted.
    message = e.stanza
    assertEquals('message', message.name)
    assertEquals(ns.CLIENT, message.uri)
    body = message.firstChildElement()
    assertEquals('body', body.name)
    assertEquals(ns.CLIENT, body.uri)

    console.Channel.Close()

if __name__ == '__main__':
    exec_test(test)
