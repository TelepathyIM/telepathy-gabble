
"""
Test MUC support.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import exec_test, make_muc_presence, request_muc_handle
from servicetest import call_async, EventPattern

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    room_handle = request_muc_handle(q, conn, stream, 'chat@conf.localhost')
    call_async(q, conn, 'RequestChannel',
        'org.freedesktop.Telepathy.Channel.Type.Text', 2, room_handle, True)

    gfc, _, _ = q.expect_many(
        EventPattern('dbus-signal', signal='GroupFlagsChanged'),
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [], [], [], [2], 0, 0]),
        EventPattern('stream-presence', to='chat@conf.localhost/test'))
    assert gfc.args[1] == 0

    # Send presence for other member of room.
    stream.send(make_muc_presence(
        'owner', 'moderator', 'chat@conf.localhost', 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence(
        'none', 'participant', 'chat@conf.localhost', 'test'))

    event = q.expect('dbus-signal', signal='MembersChanged',
        args=[u'', [2, 3], [], [], [], 0, 0])
    assert conn.InspectHandles(1, [2]) == ['chat@conf.localhost/test']
    assert conn.InspectHandles(1, [3]) == ['chat@conf.localhost/bob']

    event = q.expect('dbus-return', method='RequestChannel')
    text_chan = bus.get_object(conn.bus_name, event.value[0])

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = text_chan.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props.get('TargetHandle') == room_handle,\
            (channel_props.get('TargetHandle'), room_handle)
    assert channel_props.get('TargetHandleType') == 2,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == \
            'org.freedesktop.Telepathy.Channel.Type.Text',\
            channel_props.get('ChannelType')
    assert 'org.freedesktop.Telepathy.Channel.Interface.Group' in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert 'org.freedesktop.Telepathy.Channel.Interface.Password' in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert 'org.freedesktop.Telepathy.Properties' in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert 'org.freedesktop.Telepathy.Channel.Interface.ChatState' in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert channel_props['TargetID'] == 'chat@conf.localhost', channel_props
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == 'test@localhost'
    assert channel_props['InitiatorHandle'] == conn.GetSelfHandle()

    # Exercise Group Properties from spec 0.17.6 (in a basic way)
    group_props = text_chan.GetAll(
            'org.freedesktop.Telepathy.Channel.Interface.Group',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert 'HandleOwners' in group_props, group_props
    assert 'Members' in group_props, group_props
    assert 'LocalPendingMembers' in group_props, group_props
    assert 'RemotePendingMembers' in group_props, group_props
    assert 'GroupFlags' in group_props, group_props

    message = domish.Element((None, 'message'))
    message['from'] = 'chat@conf.localhost/bob'
    message['type'] = 'groupchat'
    body = message.addElement('body', content='hello')
    stream.send(message)

    event = q.expect('dbus-signal', signal='Received')
    # sender: bob
    assert event.args[2] == 3
    # message type: normal
    assert event.args[3] == 0
    # flags: none
    assert event.args[4] == 0
    # body
    assert event.args[5] == 'hello'

    dbus.Interface(text_chan,
            'org.freedesktop.Telepathy.Channel.Type.Text').Send(0, 'goodbye')

    event = q.expect('stream-message')
    elem = event.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'groupchat'
    body = list(event.stanza.elements())[0]
    assert body.name == 'body'
    assert body.children[0] == u'goodbye'

    # test that presence changes are sent via the MUC
    conn.Presence.SetStatus({'away':{'message':'hurrah'}})

    event = q.expect('stream-presence', to='chat@conf.localhost/test')
    elem = event.stanza
    show = [e for e in elem.elements() if e.name == 'show'][0]
    assert show
    assert show.children[0] == u'away'
    status = [e for e in elem.elements() if e.name == 'status'][0]
    assert status
    assert status.children[0] == u'hurrah'

    # test that closing the channel results in an unavailable message
    text_chan.Close()

    event = q.expect('stream-presence', to='chat@conf.localhost/test')
    elem = event.stanza
    assert elem['type'] == 'unavailable'

    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True

if __name__ == '__main__':
    exec_test(test)

