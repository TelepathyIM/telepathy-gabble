"""
Test text channel initiated by me.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import exec_test
from servicetest import call_async, EventPattern, assertEquals, wrap_channel
import constants as cs

def test(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    jid = 'foo@bar.com'
    foo_handle = conn.get_contact_handle_sync(jid)

    call_async(q, conn.Requests, 'CreateChannel', {
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_HANDLE: foo_handle })

    ret, sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        )

    text_chan = wrap_channel(bus.get_object(conn.bus_name, ret.value[0]), 'Text')

    path, props = sig.args
    assertEquals(ret.value[0], path)
    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    # check that handle type == contact handle
    assertEquals(cs.HT_CONTACT, props[cs.TARGET_HANDLE_TYPE])
    assertEquals(foo_handle, props[cs.TARGET_HANDLE])

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = text_chan.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props.get('TargetHandle') == foo_handle,\
            (channel_props.get('TargetHandle'), foo_handle)
    assert channel_props.get('TargetEntityType') == 1,\
            channel_props.get('TargetEntityType')
    assert channel_props.get('ChannelType') == \
            cs.CHANNEL_TYPE_TEXT,\
            channel_props.get('ChannelType')
    assert cs.CHANNEL_IFACE_CHAT_STATE in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert channel_props['TargetID'] == jid,\
            (channel_props['TargetID'], jid)
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorHandle'] == self_handle,\
            (channel_props['InitiatorHandle'], self_handle)
    assert channel_props['InitiatorID'] == 'test@localhost',\
            channel_props['InitiatorID']

    text_chan.send_msg_sync('hey')

    event = q.expect('stream-message')

    elem = event.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'chat'
    body = list(event.stanza.elements())[0]
    assert body.name == 'body'
    assert body.children[0] == u'hey'

    # <message type="chat"><body>hello</body</message>
    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com/Pidgin'
    m['type'] = 'chat'
    m.addElement('body', content='hello')
    stream.send(m)

    event = q.expect('dbus-signal', signal='MessageReceived')

    msg = event.args[0]
    assertEquals('hello', msg[1]['content'])

if __name__ == '__main__':
    exec_test(test)
