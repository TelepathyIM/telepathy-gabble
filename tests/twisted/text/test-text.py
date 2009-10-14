
"""
Test text channel.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import exec_test
from servicetest import EventPattern, wrap_channel, assertNotEquals
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    id = '1845a1a9-f7bc-4a2e-a885-633aadc81e1b'

    # <message type="chat"><body>hello</body</message>
    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com/Pidgin'
    m['id'] = id
    m['type'] = 'chat'
    m.addElement('body', content='hello')
    stream.send(m)

    event = q.expect('dbus-signal', signal='NewChannel')
    text_chan = wrap_channel(
        bus.get_object(conn.bus_name, event.args[0]), 'Text', ['Messages'])
    assert event.args[1] == cs.CHANNEL_TYPE_TEXT
    assert event.args[2] == cs.HT_CONTACT
    foo_at_bar_dot_com_handle = event.args[3]
    jid = conn.InspectHandles(1, [foo_at_bar_dot_com_handle])[0]
    assert jid == 'foo@bar.com'
    assert event.args[4] == False   # suppress handler

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = text_chan.Properties.GetAll(cs.CHANNEL)
    assert channel_props.get('TargetHandle') == event.args[3],\
            (channel_props.get('TargetHandle'), event.args[3])
    assert channel_props.get('TargetHandleType') == cs.HT_CONTACT,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == \
            cs.CHANNEL_TYPE_TEXT,\
            channel_props.get('ChannelType')
    assert cs.CHANNEL_IFACE_CHAT_STATE in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert cs.CHANNEL_IFACE_MESSAGES in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert channel_props['TargetID'] == jid,\
            (channel_props['TargetID'], jid)
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorHandle'] == event.args[3],\
            (channel_props['InitiatorHandle'], event.args[3])
    assert channel_props['InitiatorID'] == jid,\
            (channel_props['InitiatorID'], jid)

    received, message_received = q.expect_many(
        EventPattern('dbus-signal', signal='Received'),
        EventPattern('dbus-signal', signal='MessageReceived'),
        )

    # Check that C.T.Text.Received looks right
    # message type: normal
    assert received.args[3] == 0
    # flags: none
    assert received.args[4] == 0
    # body
    assert received.args[5] == 'hello'


    # Check that C.I.Messages.MessageReceived looks right.
    message = message_received.args[0]

    # message should have two parts: the header and one content part
    assert len(message) == 2, message
    header, body = message

    assert header['message-sender'] == foo_at_bar_dot_com_handle, header
    # the spec says that message-type "MAY be omitted for normal chat
    # messages."
    assert 'message-type' not in header or header['message-type'] == 0, header

    # This looks wrong, but is correct. We don't know if our contacts generate
    # message id='' attributes which are unique enough for our requirements, so
    # we should not use them as the message-token for incoming messages.
    assertNotEquals(id, header['message-token'])

    assert body['content-type'] == 'text/plain', body
    assert body['content'] == 'hello', body

    # Remove the message from the pending message queue, and check that
    # PendingMessagesRemoved fires.
    message_id = header['pending-message-id']

    text_chan.Text.AcknowledgePendingMessages([message_id])

    removed = q.expect('dbus-signal', signal='PendingMessagesRemoved')

    removed_ids = removed.args[0]
    assert len(removed_ids) == 1, removed_ids
    assert removed_ids[0] == message_id, (removed_ids, message_id)

    # Send a Notice using the Messages API
    greeting = [
        dbus.Dictionary({ 'message-type': 2, # Notice
                        }, signature='sv'),
        { 'content-type': 'text/plain',
          'content': u"what up",
        }
    ]

    sent_token = text_chan.Messages.SendMessage(greeting, dbus.UInt32(0))

    stream_message, sent, message_sent = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='Sent'),
        EventPattern('dbus-signal', signal='MessageSent'),
        )

    elem = stream_message.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'normal'
    body = list(stream_message.stanza.elements())[0]
    assert body.name == 'body'
    assert body.children[0] == u'what up'

    sent_message = message_sent.args[0]
    assert len(sent_message) == 2, sent_message
    header = sent_message[0]
    assert header['message-type'] == 2, header # Notice
    assert header['message-token'] == sent_token, header
    body = sent_message[1]
    assert body['content-type'] == 'text/plain', body
    assert body['content'] == u'what up', body

    assert message_sent.args[2] == sent_token

    assert sent.args[1] == 2, sent.args # Notice
    assert sent.args[2] == u'what up', sent.args


    # Send a message using Channel.Type.Text API
    text_chan.Text.Send(0, 'goodbye')

    stream_message, sent, message_sent = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='Sent'),
        EventPattern('dbus-signal', signal='MessageSent'),
        )

    elem = stream_message.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'chat'
    body = list(stream_message.stanza.elements())[0]
    assert body.name == 'body'
    assert body.children[0] == u'goodbye'

    sent_message = message_sent.args[0]
    assert len(sent_message) == 2, sent_message
    header = sent_message[0]
    # the spec says that message-type "MAY be omitted for normal chat
    # messages."
    assert 'message-type' not in header or header['message-type'] == 0, header
    body = sent_message[1]
    assert body['content-type'] == 'text/plain', body
    assert body['content'] == u'goodbye', body

    assert sent.args[1] == 0, sent.args # message type normal
    assert sent.args[2] == u'goodbye', sent.args

if __name__ == '__main__':
    exec_test(test)
