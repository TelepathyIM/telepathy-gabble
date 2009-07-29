"""
Test text channel initiated by me.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import exec_test
from servicetest import call_async, EventPattern
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    self_handle = conn.GetSelfHandle()

    jid = 'foo@bar.com'
    call_async(q, conn, 'RequestHandles', cs.HT_CONTACT, [jid])

    event = q.expect('dbus-return', method='RequestHandles')
    foo_handle = event.value[0][0]

    call_async(q, conn, 'RequestChannel',
        cs.CHANNEL_TYPE_TEXT, cs.HT_CONTACT, foo_handle, True)

    ret, sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        )

    text_chan = bus.get_object(conn.bus_name, ret.value[0])

    assert sig.args[0] == ret.value[0], \
            (sig.args[0], ret.value[0])
    assert sig.args[1] == cs.CHANNEL_TYPE_TEXT, sig.args[1]
    # check that handle type == contact handle
    assert sig.args[2] == 1, sig.args[1]
    assert sig.args[3] == foo_handle, (sig.args[3], foo_handle)
    assert sig.args[4] == True      # suppress handler

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = text_chan.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props.get('TargetHandle') == foo_handle,\
            (channel_props.get('TargetHandle'), foo_handle)
    assert channel_props.get('TargetHandleType') == 1,\
            channel_props.get('TargetHandleType')
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

    dbus.Interface(text_chan, cs.CHANNEL_TYPE_TEXT).Send(0, 'hey')

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

    event = q.expect('dbus-signal', signal='Received')

    # message type: normal
    assert event.args[3] == 0
    # flags: none
    assert event.args[4] == 0
    # body
    assert event.args[5] == 'hello'

if __name__ == '__main__':
    exec_test(test)
