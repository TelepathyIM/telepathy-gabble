
"""
Test text channel.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import exec_test

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    # <message type="chat"><body>hello</body</message>
    m = domish.Element(('', 'message'))
    m['from'] = 'foo@bar.com'
    m['type'] = 'chat'
    m.addElement('body', content='hello')
    stream.send(m)

    event = q.expect('dbus-signal', signal='NewChannel')
    text_chan = bus.get_object(conn._named_service, event.args[0])
    assert event.args[1] == u'org.freedesktop.Telepathy.Channel.Type.Text'
    # check that handle type == contact handle
    assert event.args[2] == 1
    jid = conn.InspectHandles(1, [event.args[3]])[0]
    assert jid == 'foo@bar.com'

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = text_chan.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert channel_props.get('TargetHandle') == event.args[3],\
            (channel_props.get('TargetHandle'), event.args[3])
    assert channel_props.get('TargetHandleType') == 1,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == \
            'org.freedesktop.Telepathy.Channel.Type.Text',\
            channel_props.get('ChannelType')
    assert 'org.freedesktop.Telepathy.Channel.Interface.ChatState' in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')

    event = q.expect('dbus-signal', signal='Received')

    # message type: normal
    assert event.args[3] == 0
    # flags: none
    assert event.args[4] == 0
    # body
    assert event.args[5] == 'hello'

    dbus.Interface(text_chan,
        u'org.freedesktop.Telepathy.Channel.Type.Text').Send(0, 'goodbye')

    event = q.expect('stream-message')

    elem = event.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'chat'
    body = list(event.stanza.elements())[0]
    assert body.name == 'body'
    assert body.children[0] == u'goodbye'

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

