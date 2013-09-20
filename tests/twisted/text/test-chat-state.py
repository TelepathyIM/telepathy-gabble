# coding=utf-8
"""
Test that chat state notifications are correctly sent and received on text
channels.
"""

from twisted.words.xish import domish

from servicetest import (assertEquals, assertNotEquals,
    assertLength, wrap_channel, EventPattern, call_async,
    sync_dbus)
from gabbletest import exec_test, make_result_iq, sync_stream, make_presence
import constants as cs
import ns

def check_state_notification(elem, name, allow_body=False):
    assertEquals('message', elem.name)
    assertEquals('chat', elem['type'])

    children = list(elem.elements())
    notification = [x for x in children if x.uri == ns.CHAT_STATES][0]
    assert notification.name == name, notification.toXml()

    if not allow_body:
        assert len(children) == 1, elem.toXml()

def make_message(jid, body=None, state=None):
    m = domish.Element((None, 'message'))
    m['from'] = jid
    m['type'] = 'chat'

    if state is not None:
        m.addElement((ns.CHAT_STATES, state))

    if body is not None:
        m.addElement('body', content=body)

    return m

def test(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    jid = 'foo@bar.com'
    full_jid = 'foo@bar.com/Foo'
    foo_handle = conn.get_contact_handle_sync(jid)

    path = conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: foo_handle,
              })[0]
    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text',
        ['ChatState', 'Destroyable'])

    presence = make_presence(full_jid, status='hello',
        caps={
            'node': 'http://telepathy.freedesktop.org/homeopathy',
            'ver' : '0.1',
        })
    stream.send(presence)

    version_event = q.expect('stream-iq', to=full_jid,
        query_ns=ns.DISCO_INFO,
        query_node='http://telepathy.freedesktop.org/homeopathy#0.1')

    result = make_result_iq(stream, version_event.stanza)
    query = result.firstChildElement()
    feature = query.addElement('feature')
    feature['var'] = ns.CHAT_STATES
    stream.send(result)

    sync_stream(q, stream)

    states = chan.Properties.Get(cs.CHANNEL_IFACE_CHAT_STATE, 'ChatStates')
    assertEquals(cs.CHAT_STATE_INACTIVE,
            states.get(self_handle, cs.CHAT_STATE_INACTIVE))
    assertEquals(cs.CHAT_STATE_INACTIVE,
            states.get(foo_handle, cs.CHAT_STATE_INACTIVE))

    # Receiving chat states:

    # Composing...
    stream.send(make_message(full_jid, state='composing'))

    changed = q.expect('dbus-signal', signal='ChatStateChanged',
            path=chan.object_path)
    handle, state = changed.args
    assertEquals(foo_handle, handle)
    assertEquals(cs.CHAT_STATE_COMPOSING, state)

    states = chan.Properties.Get(cs.CHANNEL_IFACE_CHAT_STATE, 'ChatStates')
    assertEquals(cs.CHAT_STATE_INACTIVE,
            states.get(self_handle, cs.CHAT_STATE_INACTIVE))
    assertEquals(cs.CHAT_STATE_COMPOSING,
            states.get(foo_handle, cs.CHAT_STATE_INACTIVE))

    # Message!
    stream.send(make_message(full_jid, body='hello', state='active'))

    changed = q.expect('dbus-signal', signal='ChatStateChanged',
            path=chan.object_path)
    handle, state = changed.args
    assertEquals(foo_handle, handle)
    assertEquals(cs.CHAT_STATE_ACTIVE, state)

    states = chan.Properties.Get(cs.CHANNEL_IFACE_CHAT_STATE, 'ChatStates')
    assertEquals(cs.CHAT_STATE_INACTIVE,
            states.get(self_handle, cs.CHAT_STATE_INACTIVE))
    assertEquals(cs.CHAT_STATE_ACTIVE,
            states.get(foo_handle, cs.CHAT_STATE_INACTIVE))

    # Assert that a redundant chat-state change doesn't emit a signal

    forbidden = [EventPattern('dbus-signal', signal='ChatStateChanged',
        args=[foo_handle, cs.CHAT_STATE_ACTIVE])]
    q.forbid_events(forbidden)

    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com/Foo'
    m['type'] = 'chat'
    m.addElement((ns.CHAT_STATES, 'active'))
    m.addElement('body', content='hello')
    stream.send(m)

    sync_dbus(bus, q, conn)
    sync_stream(q, stream)

    q.unforbid_events(forbidden)

    # Sending chat states:

    # Composing...
    chan.ChatState.SetChatState(cs.CHAT_STATE_COMPOSING)

    stream_message = q.expect('stream-message')
    check_state_notification(stream_message.stanza, 'composing')

    states = chan.Properties.Get(cs.CHANNEL_IFACE_CHAT_STATE, 'ChatStates')
    assertEquals(cs.CHAT_STATE_COMPOSING,
            states.get(self_handle, cs.CHAT_STATE_INACTIVE))
    assertEquals(cs.CHAT_STATE_ACTIVE,
            states.get(foo_handle, cs.CHAT_STATE_INACTIVE))

    # XEP 0085:
    #   every content message SHOULD contain an <active/> notification.
    chan.Text.Send(0, 'hi.')

    stream_message = q.expect('stream-message')
    elem = stream_message.stanza
    assertEquals('chat', elem['type'])

    check_state_notification(elem, 'active', allow_body=True)

    states = chan.Properties.Get(cs.CHANNEL_IFACE_CHAT_STATE, 'ChatStates')
    assertEquals(cs.CHAT_STATE_ACTIVE,
            states.get(self_handle, cs.CHAT_STATE_INACTIVE))
    assertEquals(cs.CHAT_STATE_ACTIVE,
            states.get(foo_handle, cs.CHAT_STATE_INACTIVE))

    def is_body(e):
        if e.name == 'body':
            assert e.children[0] == u'hi.', e.toXml()
            return True
        return False

    assert len([x for x in elem.elements() if is_body(x)]) == 1, elem.toXml()

    # Close the channel without acking the received message. The peer should
    # get a <gone/> notification, and the channel should respawn.
    chan.Close()

    gone, _, _ = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='NewChannel'),
        )
    check_state_notification(gone.stanza, 'gone')

    # Reusing the proxy object because we happen to know it'll be at the same
    # path...

    # Destroy the channel. The peer shouldn't get a <gone/> notification, since
    # we already said we were gone and haven't sent them any messages to the
    # contrary.
    es = [EventPattern('stream-message')]
    q.forbid_events(es)

    chan.Destroyable.Destroy()
    sync_stream(q, stream)

    # Make the channel anew.
    path = conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: foo_handle,
              })[0]
    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text',
        ['ChatState', 'Destroyable'])

    # Close it immediately; the peer should again not get a <gone/>
    # notification, since we haven't sent any notifications on that channel.
    chan.Close()
    sync_stream(q, stream)
    q.unforbid_events(es)

    # XEP-0085 §5.1 defines how to negotiate support for chat states with a
    # contact in the absence of capabilities. This is useful when talking to
    # invisible contacts, for example.

    # First, if we receive a message from a contact, containing an <active/>
    # notification, they support chat states, so we should send them.

    jid = 'i@example.com'
    full_jid = jid + '/GTalk'

    path = conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_ID: jid,
              })[0]
    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text',
        ['ChatState'])

    stream.send(make_message(full_jid, body='i am invisible', state='active'))

    changed = q.expect('dbus-signal', signal='ChatStateChanged',
            path=chan.object_path)
    assertEquals(cs.CHAT_STATE_ACTIVE, changed.args[1])

    # We've seen them send a chat state notification, so we should send them
    # notifications when the UI tells us to.
    chan.ChatState.SetChatState(cs.CHAT_STATE_COMPOSING)
    stream_message = q.expect('stream-message', to=full_jid)
    check_state_notification(stream_message.stanza, 'composing')

    changed = q.expect('dbus-signal', signal='ChatStateChanged',
            path=chan.object_path)
    handle, state = changed.args
    assertEquals(cs.CHAT_STATE_COMPOSING, state)
    assertEquals(self_handle, handle)

    chan.Text.Send(0, 'very convincing')
    stream_message = q.expect('stream-message', to=full_jid)
    check_state_notification(stream_message.stanza, 'active', allow_body=True)

    # Now, test the case where we start the negotiation, and the contact
    # turns out to support chat state notifications.

    jid = 'c@example.com'
    full_jid = jid + '/GTalk'
    path = conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_ID: jid,
              })[0]
    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text',
        ['ChatState'])

    # We shouldn't send any notifications until we actually send a message.
    # But ChatStateChanged is still emitted locally
    e = EventPattern('stream-message', to=jid)
    q.forbid_events([e])
    for i in [cs.CHAT_STATE_ACTIVE, cs.CHAT_STATE_COMPOSING,
              cs.CHAT_STATE_PAUSED, cs.CHAT_STATE_INACTIVE ]:
        chan.ChatState.SetChatState(i)
        changed = q.expect('dbus-signal', signal='ChatStateChanged',
                path=chan.object_path)
        handle, state = changed.args
        assertEquals(i, state)
        assertEquals(self_handle, handle)

    sync_stream(q, stream)
    q.unforbid_events([e])

    # When we send a message, say we're active.
    chan.Text.Send(0, 'is anyone there?')
    stream_message = q.expect('stream-message', to=jid)
    check_state_notification(stream_message.stanza, 'active', allow_body=True)

    # The D-Bus property changes, too
    changed = q.expect('dbus-signal', signal='ChatStateChanged',
            path=chan.object_path)
    handle, state = changed.args
    assertEquals(cs.CHAT_STATE_ACTIVE, state)
    assertEquals(self_handle, handle)

    # We get a notification back from our contact.
    stream.send(make_message(full_jid, state='composing'))

    # Wait until gabble tells us the chat-state of the remote party has
    # changed so we know gabble knows chat state notification are supported
    changed = q.expect('dbus-signal', signal='ChatStateChanged',
            path=chan.object_path)
    handle, state = changed.args
    assertEquals(cs.CHAT_STATE_COMPOSING, state)
    assertNotEquals(foo_handle, handle)

    # So now we know they support notification, so should send notifications.
    chan.ChatState.SetChatState(cs.CHAT_STATE_COMPOSING)

    # This doesn't check whether we're sending to the bare jid, or the
    # jid+resource. In fact, the notification is sent to the bare jid, because
    # we only update which jid we send to when we actually receive a message,
    # not when we receive a notification. wjt thinks this is less surprising
    # than the alternative:
    #
    #  • I'm talking to you on my N900, and signed in on my laptop;
    #  • I enter one character in a tab to you on my laptop, and then delete
    #    it;
    #  • Now your messages to me appear on my laptop (until I send you another
    #    one from my N900)!
    stream_message = q.expect('stream-message')
    check_state_notification(stream_message.stanza, 'composing')

    # The D-Bus property changes, too
    changed = q.expect('dbus-signal', signal='ChatStateChanged',
            path=chan.object_path)
    handle, state = changed.args
    assertEquals(cs.CHAT_STATE_COMPOSING, state)
    assertEquals(self_handle, handle)

    # But! Now they start messaging us from a different client, which *doesn't*
    # support notifications.
    other_jid = jid + '/Library'
    stream.send(make_message(other_jid, body='grr, library computers'))
    q.expect('dbus-signal', signal='Received')

    # Okay, we should stop sending typing notifications.
    e = EventPattern('stream-message', to=other_jid)
    q.forbid_events([e])
    for i in [cs.CHAT_STATE_COMPOSING, cs.CHAT_STATE_INACTIVE,
              cs.CHAT_STATE_PAUSED, cs.CHAT_STATE_ACTIVE]:
        chan.ChatState.SetChatState(i)
    sync_stream(q, stream)
    q.unforbid_events([e])

    # Now, test the case where we start the negotiation, and the contact
    # does not support chat state notifications

    jid = 'twitterbot@example.com'
    full_jid = jid + '/Nonsense'
    path = conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_ID: jid,
              })[0]
    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text',
        ['ChatState'])

    # We shouldn't send any notifications until we actually send a message.
    e = EventPattern('stream-message', to=jid)
    q.forbid_events([e])
    for i in [cs.CHAT_STATE_COMPOSING, cs.CHAT_STATE_INACTIVE,
              cs.CHAT_STATE_PAUSED, cs.CHAT_STATE_ACTIVE]:
        chan.ChatState.SetChatState(i)
    sync_stream(q, stream)
    q.unforbid_events([e])

    # When we send a message, say we're active.
    chan.Text.Send(0, '#n900 #maemo #zomg #woo #yay http://bit.ly/n900')
    stream_message = q.expect('stream-message', to=jid)
    check_state_notification(stream_message.stanza, 'active', allow_body=True)

    # They reply without a chat state.
    stream.send(make_message(full_jid, body="posted."))
    q.expect('dbus-signal', signal='Received')

    # Okay, we shouldn't send any more.
    e = EventPattern('stream-message', to=other_jid)
    q.forbid_events([e])
    for i in [cs.CHAT_STATE_COMPOSING, cs.CHAT_STATE_INACTIVE,
              cs.CHAT_STATE_PAUSED, cs.CHAT_STATE_ACTIVE]:
        chan.ChatState.SetChatState(i)
    sync_stream(q, stream)
    q.unforbid_events([e])

    chan.Text.Send(0, '@stephenfry simmer down')
    message = q.expect('stream-message')
    states = [x for x in message.stanza.elements() if x.uri == ns.CHAT_STATES]
    assertLength(0, states)

if __name__ == '__main__':
    exec_test(test)
