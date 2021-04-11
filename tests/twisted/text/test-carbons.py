
"""
Test text channel with carbons.
"""

from gabbletest import XmppXmlStream, exec_test, elem, acknowledge_iq
from servicetest import (EventPattern, wrap_channel, assertEquals, assertLength,
        assertContains, sync_dbus)
import constants as cs

NS_CARBONS = 'urn:xmpp:carbons:2'
NS_FORWARD = 'urn:xmpp:forward:0'

class CarbonStream(XmppXmlStream):
    disco_features = [
        NS_CARBONS,
    ]
# Same as test-text.py but using carbon-forwarded message envelopes
def test(q, bus, conn, stream):
    id = '1845a1a9-f7bc-4a2e-a885-633aadc81e1b'

    iq = q.expect('stream-iq', iq_type='set', query_ns=NS_CARBONS)
    assert iq.stanza.enable
    acknowledge_iq(stream, iq.stanza)

    # <message type="chat"><body>hello</body</message>
    msg = elem('message', type='chat', from_='test@localhost')(
        elem(NS_CARBONS, 'received')(
            elem(NS_FORWARD, 'forwarded')(
                elem('jabber:client','message', id=id, from_='foo@bar.com/Pidgin', type='chat')(
                    elem('body')('hello')
                )
            )
        )
    )
    stream.send(msg)

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

    # And now let's try a message with a malformed type='' attribute.
    malformed = elem('message')(elem(NS_CARBONS, 'received')(elem(NS_FORWARD,'forwarded')(
        elem('message', from_='foo@bar.com/fubber', type="'")(
          elem('body')(u'Internettt!'),
          elem('subject')(u'xyzzy'),
          elem('thread')(u'6666'),
        )
    )))
    stream.send(malformed)

    event = q.expect('dbus-signal', signal='MessageReceived')
    message, = event.args
    assertLength(2, message)
    header, body = message

    # Gabble should treat the unparseable type as if it were 'normal' or
    # omitted (not to be confused with Telepathy's Normal, which is 'chat' in
    # XMPP...)
    assertEquals(cs.MT_NOTICE, header['message-type'])

    # In addition to above standard tests, the tricky one is 'sent' carbon
    sent_token = id[:8:][::-1]+id[8::]
    msg = elem('message', type='chat', from_='test@localhost')(
        elem(NS_CARBONS, 'sent')(
            elem(NS_FORWARD, 'forwarded')(
                elem('jabber:client','message', id=sent_token, from_='test@localhost/Gabble', to='foo@bar.com/Pidgin')(
                    elem('body')('what up')
                )
            )
        )
    )
    stream.send(msg)
    message_received = q.expect('dbus-signal', signal='MessageReceived')
    sent_message = message_received.args[0]

    assert len(sent_message) == 2, sent_message
    header = sent_message[0]
    assert header['message-type'] == 2, header # Notice
    assert header['message-token'] == sent_token, header
    assertEquals(conn.Properties.Get(cs.CONN, "SelfHandle"), header['message-sender'])
    assertEquals('test@localhost', header['message-sender-id'])
    body = sent_message[1]
    assert body['content-type'] == 'text/plain', body
    assert body['content'] == u'what up', body

    # Remove the message from the pending message queue, and check that
    # PendingMessagesRemoved fires.
    message_id = header['pending-message-id']

    text_chan.Text.AcknowledgePendingMessages([message_id])

    removed = q.expect('dbus-signal', signal='PendingMessagesRemoved')

    removed_ids = removed.args[0]
    assert len(removed_ids) == 1, removed_ids
    assert removed_ids[0] == message_id, (removed_ids, message_id)

    # And last one, normal chat (whatever that means)
    di = id[::-1]
    msg = elem('message', type='chat', from_='test@localhost')(
        elem(NS_CARBONS, 'sent')(
            elem(NS_FORWARD, 'forwarded')(
                elem('jabber:client','message', id=di, type='chat', from_='test@localhost/Gabble', to='foo@bar.com/Pidgin')(
                    elem('body')('goodbye')
                )
            )
        )
    )
    stream.send(msg)
    message_received = q.expect('dbus-signal', signal='MessageReceived')
    sent_message = message_received.args[0]
    assert len(sent_message) == 2, sent_message
    header = sent_message[0]
    # the spec says that message-type "MAY be omitted for normal chat
    # messages."
    assert 'message-type' not in header or header['message-type'] == 0, header
    assert header['message-token'] == di, header
    assertEquals(conn.Properties.Get(cs.CONN, "SelfHandle"), header['message-sender'])
    assertEquals('test@localhost', header['message-sender-id'])
    body = sent_message[1]
    assert body['content-type'] == 'text/plain', body
    assert body['content'] == u'goodbye', body

    # Verify source protection
    msg = elem('message', type='chat', from_='smith@matrix.org/agent712')(
        elem(NS_CARBONS, 'received')(
            elem(NS_FORWARD, 'forwarded')(
                elem('jabber:client','message', id=id, from_='foo@bar.com/Pidgin', type='chat')(
                    elem('body')('Mr. Anderson!')
                )
            )
        )
    )
    q.forbid_events([EventPattern('dbus-signal', signal='MessageReceived')])
    stream.send(msg)
    sync_dbus(bus, q, conn)
    q.unforbid_all()

    # And MUC - demo attack vector
    msg = elem('message')(
        elem(NS_CARBONS, 'received')(
            elem(NS_FORWARD, 'forwarded')(
                elem('jabber:client','message', id=sent_token, from_='foo@bar.com/Pidgin', to='test@localhost')(
                    elem('body')('oh btb')
               )
            )
        ),
        elem('jabber:x:conference', 'x', jid='room@localhost')
     )
    stream.send(msg)
    event = q.expect('stream-iq', iq_type='get', query_ns='http://jabber.org/protocol/disco#info', to='room@localhost')

    # MUC Invite Attack execution
    msg = elem('message', from_='smith@matrix.org/agent712')(
        elem(NS_CARBONS, 'received')(
            elem(NS_FORWARD, 'forwarded')(
                elem('jabber:client','message', id=sent_token, from_='foo@bar.com/Pidgin', to='test@localhost')(
                    elem('body')('Nice party here, really')
               )
            )
        ),
        elem('jabber:x:conference', 'x', jid='crimescene@set.up')
     )
    q.forbid_events([EventPattern('dbus-signal', signal='MessageReceived')])
    stream.send(msg)
    sync_dbus(bus, q, conn)
    q.unforbid_all()

if __name__ == '__main__':
    exec_test(test, protocol=CarbonStream, params={'message-carbons':True})
