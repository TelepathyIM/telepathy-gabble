
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

    assert body['content-type'] == 'text/plain', body
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
        { 'content-type': 'text/plain',
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
    assert body['content-type'] == 'text/plain', body
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

    # Check that we got the corresponding delivery report
    report, old_received = q.expect_many(
        EventPattern('dbus-signal', signal='MessageReceived'),
        EventPattern('dbus-signal', signal='Received'),
        )

    assert len(report.args) == 1, report.args
    parts = report.args[0]
    # The delivery report should just be a header, no body.
    assert len(parts) == 1, parts
    part = parts[0]
    # The intended recipient was the MUC, so there's no contact handle
    # suitable for being 'message-sender'.
    assert 'message-sender' not in part or part['message-sender'] == 0, part
    assert part['message-type'] == 4, part # Message_Type_Delivery_Report
    assert part['delivery-status'] == 1, part # Delivery_Status_Delivered
    # Gabble doesn't issue tokens for messages you send, so no token should be
    # in the report
    assert 'delivery-token' not in part, part
    assert 'delivery-error' not in part, part
    assert 'delivery-echo' in part, part

    # Check that the included echo is from us, and matches all the keys in the
    # message we sent.
    echo = part['delivery-echo']
    assert len(echo) == len(greeting), (echo, greeting)
    # Earlier in this test we checked that handle 2 is us.
    assert echo[0]['message-sender'] == 2, echo[0]
    for i in range(0, len(echo)):
        for key in greeting[i]:
            assert key in echo[i], (i, key, echo)
            assert echo[i][key] == greeting[i][key], (i, key, echo, greeting)

    # The Text.Received signal should be a "you're not tall enough" stub
    id, timestamp, sender, type, flags, text = old_received.args
    assert sender == 0, old_received.args
    assert type == 4, old_received.args # Message_Type_Delivery_Report
    assert flags == 2, old_received.args # Non_Text_Content
    assert text == '', old_received.args


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
    assert body['content-type'] == 'text/plain', body
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

