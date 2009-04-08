
"""
Test that chat state notifications are correctly sent and received on text
channels.
"""

from twisted.words.xish import domish

from servicetest import call_async, make_channel_proxy
from gabbletest import exec_test, make_result_iq, sync_stream, make_presence

import ns

CHAT_STATE_ACTIVE = 2
CHAT_STATE_COMPOSING = 4

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()

    jid = 'foo@bar.com'
    foo_handle = conn.RequestHandles(1, [jid])[0]

    call_async(q, conn.Requests, 'CreateChannel',
            { 'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Text',
              'org.freedesktop.Telepathy.Channel.TargetHandleType': 1,
              'org.freedesktop.Telepathy.Channel.TargetHandle': foo_handle,
              })

    ret = q.expect('dbus-return', method='CreateChannel')
    path = ret.value[0]
    text_chan = bus.get_object(conn.bus_name, path)
    text_iface = make_channel_proxy(conn, path, 'Channel.Type.Text')
    chat_state_iface = make_channel_proxy(conn, path,
        'Channel.Interface.ChatState')

    presence = make_presence('foo@bar.com/Foo', status='hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = 'http://telepathy.freedesktop.org/homeopathy'
    c['ver'] = '0.1'
    stream.send(presence)

    version_event = q.expect('stream-iq', to='foo@bar.com/Foo',
        query_ns='http://jabber.org/protocol/disco#info',
        query_node='http://telepathy.freedesktop.org/homeopathy#0.1')

    result = make_result_iq(stream, version_event.stanza)
    query = result.firstChildElement()
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/chatstates'
    stream.send(result)

    sync_stream(q, stream)

    # Receiving chat states:

    # Composing...
    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com/Foo'
    m['type'] = 'chat'
    m.addElement((ns.CHAT_STATES, 'composing'))
    stream.send(m)

    changed = q.expect('dbus-signal', signal='ChatStateChanged')
    handle, state = changed.args
    assert handle == foo_handle, (handle, foo_handle)
    assert state == CHAT_STATE_COMPOSING, (state, CHAT_STATE_COMPOSING)

    # Message!

    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com/Foo'
    m['type'] = 'chat'
    m.addElement((ns.CHAT_STATES, 'active'))
    m.addElement('body', content='hello')
    stream.send(m)

    changed = q.expect('dbus-signal', signal='ChatStateChanged')
    handle, state = changed.args
    assert handle == foo_handle, (handle, foo_handle)
    assert state == CHAT_STATE_ACTIVE, (state, CHAT_STATE_ACTIVE)

    # Sending chat states:

    # Composing...
    call_async(q, chat_state_iface, 'SetChatState', CHAT_STATE_COMPOSING)

    stream_message = q.expect('stream-message')
    elem = stream_message.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'normal'
    chrilden = list(elem.elements())
    assert len(chrilden) == 1, elem.toXml()
    composing = chrilden[0]
    assert composing.name == 'composing', composing.toXml()
    assert composing.uri == ns.CHAT_STATES, composing.toXml()

    # XEP 0085:
    #   every content message SHOULD contain an <active/> notification.
    call_async(q, text_iface, 'Send', 0, 'hi.')

    stream_message = q.expect('stream-message')
    elem = stream_message.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'chat', elem['type']

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

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

