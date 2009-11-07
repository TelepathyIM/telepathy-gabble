
"""
Test that chat state notifications are correctly sent and received on text
channels.
"""

from twisted.words.xish import domish

from servicetest import assertEquals, wrap_channel, EventPattern
from gabbletest import exec_test, make_result_iq, sync_stream, make_presence
import constants as cs
import ns

def check_state_notification(elem, name):
    assertEquals('message', elem.name)
    assertEquals('normal', elem['type'])

    children = list(elem.elements())
    assert len(children) == 1, elem.toXml()
    notification = children[0]

    assert notification.name == name, notification.toXml()
    assert notification.uri == ns.CHAT_STATES, notification.toXml()

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
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    self_handle = conn.GetSelfHandle()

    jid = 'foo@bar.com'
    full_jid = 'foo@bar.com/Foo'
    foo_handle = conn.RequestHandles(cs.HT_CONTACT, [jid])[0]

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

    # Receiving chat states:

    # Composing...
    stream.send(make_message(full_jid, state='composing'))

    changed = q.expect('dbus-signal', signal='ChatStateChanged')
    handle, state = changed.args
    assertEquals(foo_handle, handle)
    assertEquals(cs.CHAT_STATE_COMPOSING, state)

    # Message!
    stream.send(make_message(full_jid, body='hello', state='active'))

    changed = q.expect('dbus-signal', signal='ChatStateChanged')
    handle, state = changed.args
    assertEquals(foo_handle, handle)
    assertEquals(cs.CHAT_STATE_ACTIVE, state)

    # Sending chat states:

    # Composing...
    chan.ChatState.SetChatState(cs.CHAT_STATE_COMPOSING)

    stream_message = q.expect('stream-message')
    check_state_notification(stream_message.stanza, 'composing')

    # XEP 0085:
    #   every content message SHOULD contain an <active/> notification.
    chan.Text.Send(0, 'hi.')

    stream_message = q.expect('stream-message')
    elem = stream_message.stanza
    assertEquals('chat', elem['type'])

    def is_body(e):
        if e.name == 'body':
            assert e.children[0] == u'hi.', e.toXml()
            return True
        return False

    def is_active(e):
        if e.uri == ns.CHAT_STATES:
            assert e.name == 'active', e.toXml()
            return True
        return False

    children = list(elem.elements())

    assert len(filter(is_body,   children)) == 1, elem.toXml()
    assert len(filter(is_active, children)) == 1, elem.toXml()

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

if __name__ == '__main__':
    exec_test(test)
