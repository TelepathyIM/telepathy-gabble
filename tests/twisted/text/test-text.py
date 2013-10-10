
"""
Test text channel.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import exec_test, elem
from servicetest import (EventPattern, wrap_channel, assertEquals, assertLength,
        assertContains)
import constants as cs

def test(q, bus, conn, stream):
    id = '1845a1a9-f7bc-4a2e-a885-633aadc81e1b'

    # <message type="chat"><body>hello</body</message>
    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com/Pidgin'
    m['id'] = id
    m['type'] = 'chat'
    m.addElement('body', content='hello')
    stream.send(m)

    event = q.expect('dbus-signal', signal='NewChannels')
    path, props = event.args[0][0]
    text_chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text')
    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, props[cs.TARGET_HANDLE_TYPE])
    foo_at_bar_dot_com_handle = props[cs.TARGET_HANDLE]
    jid = conn.inspect_contact_sync(foo_at_bar_dot_com_handle)
    assertEquals('foo@bar.com', jid)

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = text_chan.Properties.GetAll(cs.CHANNEL)
    assertEquals(props[cs.TARGET_HANDLE], channel_props.get('TargetHandle'))
    assertEquals(cs.HT_CONTACT, channel_props.get('TargetHandleType'))
    assertEquals(cs.CHANNEL_TYPE_TEXT, channel_props.get('ChannelType'))
    assertContains(cs.CHANNEL_IFACE_CHAT_STATE, channel_props.get('Interfaces'))
    assertContains(cs.CHANNEL_IFACE_MESSAGES, channel_props.get('Interfaces'))
    assertEquals(jid, channel_props['TargetID'])
    assertEquals(False, channel_props['Requested'])
    assertEquals(props[cs.INITIATOR_HANDLE], channel_props['InitiatorHandle'])
    assertEquals(jid, channel_props['InitiatorID'])

    message_received = q.expect('dbus-signal', signal='MessageReceived')

    # Check that C.I.Messages.MessageReceived looks right.
    message = message_received.args[0]

    # message should have two parts: the header and one content part
    assert len(message) == 2, message
    header, body = message

    assert header['message-sender'] == foo_at_bar_dot_com_handle, header
    # the spec says that message-type "MAY be omitted for normal chat
    # messages."
    assert 'message-type' not in header or header['message-type'] == 0, header

    # We don't make any uniqueness guarantees about the tokens on incoming
    # messages, so we use the id='' provided at the protocol level.
    assertEquals(id, header['message-token'])

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

    stream_message, message_sent = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='MessageSent'),
        )

    elt = stream_message.stanza
    assert elt.name == 'message'
    assert elt['type'] == 'normal'
    body = list(stream_message.stanza.elements())[0]
    assert body.name == 'body'
    assert body.children[0] == u'what up'

    sent_message = message_sent.args[0]
    assert len(sent_message) == 2, sent_message
    header = sent_message[0]
    assert header['message-type'] == 2, header # Notice
    assert header['message-token'] == sent_token, header
    assertEquals(conn.Properties.Get(cs.CONN, "SelfHandle"), header['message-sender'])
    assertEquals('test@localhost', header['message-sender-id'])
    body = sent_message[1]
    assert body['content-type'] == 'text/plain', body
    assert body['content'] == u'what up', body

    assert message_sent.args[2] == sent_token

    # Send a message using Channel.Type.Text API
    text_chan.Text.Send(0, 'goodbye')

    stream_message, message_sent = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='MessageSent'),
        )

    elt = stream_message.stanza
    assert elt.name == 'message'
    assert elt['type'] == 'chat'
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

    # And now let's try a message with a malformed type='' attribute.
    malformed = elem(
        'message', from_='foo@bar.com/fubber', type="'")(
          elem('body')(u'Internettt!'),
          elem('subject')(u'xyzzy'),
          elem('thread')(u'6666'),
        )
    stream.send(malformed)

    event = q.expect('dbus-signal', signal='MessageReceived')
    message, = event.args
    assertLength(2, message)
    header, body = message

    # Gabble should treat the unparseable type as if it were 'normal' or
    # omitted (not to be confused with Telepathy's Normal, which is 'chat' in
    # XMPP...)
    assertEquals(cs.MT_NOTICE, header['message-type'])

if __name__ == '__main__':
    exec_test(test)
