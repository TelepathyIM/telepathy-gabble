
"""
Test MUC support.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import go, make_result_iq, exec_test
from servicetest import call_async, lazy, match, EventPattern

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    # Need to call this asynchronously as it involves Gabble sending us a
    # query.
    call_async(q, conn, 'RequestHandles', 2, ['chat@conf.localhost'])

    event = q.expect('stream-iq', to='conf.localhost',
        query_ns='http://jabber.org/protocol/disco#info')
    result = make_result_iq(stream, event.stanza)
    feature = result.firstChildElement().addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/muc'
    stream.send(result)

    event = q.expect('dbus-return', method='RequestHandles')
    room_handle = event.value[0][0]

    call_async(q, conn, 'RequestChannel',
        'org.freedesktop.Telepathy.Channel.Type.Text', 2, room_handle, True)

    gfc, _, _ = q.expect_many(
        EventPattern('dbus-signal', signal='GroupFlagsChanged'),
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [], [], [], [2], 0, 0]),
        EventPattern('stream-presence', to='chat@conf.localhost/test'))
    assert gfc.args[1] == 0

    # Send presence for other member of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/bob'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    stream.send(presence)

    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/test'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    stream.send(presence)

    event = q.expect('dbus-signal', signal='MembersChanged',
        args=[u'', [2, 3], [], [], [], 0, 0])
    assert conn.InspectHandles(1, [2]) == ['chat@conf.localhost/test']
    assert conn.InspectHandles(1, [3]) == ['chat@conf.localhost/bob']

    event = q.expect('dbus-return', method='RequestChannel')

    text_chan = bus.get_object(conn.bus_name, event.value[0])

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = text_chan.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
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
    assert 'org.freedesktop.Telepathy.Channel.Interface.Messages.DRAFT' in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert channel_props['TargetID'] == 'chat@conf.localhost', channel_props
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == 'test@localhost'
    assert channel_props['InitiatorHandle'] == conn.GetSelfHandle()

    # Exercise Group Properties from spec 0.17.6 (in a basic way)
    group_props = text_chan.GetAll(
            'org.freedesktop.Telepathy.Channel.Interface.Group',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert 'HandleOwners' in group_props, group_props
    assert 'Members' in group_props, group_props
    assert 'LocalPendingMembers' in group_props, group_props
    assert 'RemotePendingMembers' in group_props, group_props
    assert 'GroupFlags' in group_props, group_props


    # Test receiving a message from Bob in the MUC
    message = domish.Element((None, 'message'))
    message['from'] = 'chat@conf.localhost/bob'
    message['type'] = 'groupchat'
    body = message.addElement('body', content='hello')
    stream.send(message)

    received, message_received = q.expect_many(
        EventPattern('dbus-signal', signal='Received'),
        EventPattern('dbus-signal', signal='MessageReceived'),
        )

    # Check Channel.Type.Text.Received:
    # sender: bob
    assert received.args[2] == 3
    # message type: normal
    assert received.args[3] == 0
    # flags: none
    assert received.args[4] == 0
    # body
    assert received.args[5] == 'hello'

    # Check Channel.Interface.Messages.MessageReceived:
    message = message_received.args[0]

    # message should have two parts: the header and one content part
    assert len(message) == 2, message
    header, body = message

    # 3 is bob
    assert header['message-sender'] == 3, header
    # the spec says that message-type "SHOULD be omitted for normal chat
    # messages."
    assert 'message-type' not in header, header

    assert body['type'] == 'text/plain', body
    assert body['content'] == 'hello', body


    # Remove the message from the pending message queue, and check that
    # PendingMessagesRemoved fires.
    message_id = header['pending-message-id']

    dbus.Interface(text_chan,
        u'org.freedesktop.Telepathy.Channel.Type.Text'
        ).AcknowledgePendingMessages([message_id])

    removed = q.expect('dbus-signal', signal='PendingMessagesRemoved')

    removed_ids = removed.args[0]
    assert len(removed_ids) == 1, removed_ids
    assert removed_ids[0] == message_id, (removed_ids, message_id)


    # Send an action using the Messages API
    greeting = [
        dbus.Dictionary({ 'message-type': 1, # Action
                        }, signature='sv'),
        { 'type': 'text/plain',
          'content': u"peers through a gap in the curtains",
        }
    ]

    dbus.Interface(text_chan,
        u'org.freedesktop.Telepathy.Channel.Interface.Messages.DRAFT'
        ).SendMessage(greeting, dbus.UInt32(0))

    stream_message, sent, message_sent = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='Sent'),
        EventPattern('dbus-signal', signal='MessageSent'),
        )

    sent_message = message_sent.args[0]
    assert len(sent_message) == 2, sent_message
    header = sent_message[0]
    assert header['message-type'] == 1, header # Action
    body = sent_message[1]
    assert body['type'] == 'text/plain', body
    assert body['content'] == u'peers through a gap in the curtains', body

    assert sent.args[1] == 1, sent.args # Action
    assert sent.args[2] == u'peers through a gap in the curtains', sent.args

    elem = stream_message.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'groupchat', repr(elem)
    assert elem['to'] == 'chat@conf.localhost', repr(elem)
    for sub_elem in stream_message.stanza.elements():
        if sub_elem.name == 'body':
            found_body = True
            assert sub_elem.children[0] == u'/me peers through a gap in the curtains'
            break
    assert found_body


    # reflect the sent message back to the MUC
    elem['from'] = 'chat@conf.localhost/test'
    stream.send(elem)

    # TODO: check for a delivery report


    # Send a normal message using the Channel.Type.Text API
    dbus.Interface(text_chan,
            'org.freedesktop.Telepathy.Channel.Type.Text').Send(0, 'goodbye')


    event, sent, message_sent = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='Sent'),
        EventPattern('dbus-signal', signal='MessageSent'),
        )

    sent_message = message_sent.args[0]
    assert len(sent_message) == 2, sent_message
    header = sent_message[0]
    assert 'message-type' not in header, header # Normal
    body = sent_message[1]
    assert body['type'] == 'text/plain', body
    assert body['content'] == u'goodbye', body

    assert sent.args[1] == 0, sent.args # Normal
    assert sent.args[2] == u'goodbye', sent.args

    elem = event.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'groupchat'
    body = list(event.stanza.elements())[0]
    assert body.name == 'body'
    assert body.children[0] == u'goodbye'

    # reflect the sent message back to the MUC
    elem['from'] = 'chat@conf.localhost/test'
    stream.send(elem)

    # TODO: check for a delivery report.


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

