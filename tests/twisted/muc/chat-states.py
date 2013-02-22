"""
Regression test for <https://bugs.freedesktop.org/show_bug.cgi?id=32952>,
wherein chat states in MUCs were misparsed, and MUC chat states in general.
"""

from servicetest import assertEquals, assertLength
from gabbletest import exec_test, elem
from mucutil import join_muc_and_check
import ns
import constants as cs

MUC = 'ohai@groupchat.google.com'
BOB = MUC + '/bob'

def check_state_notification(elem, name, allow_body=False):
    assertEquals('message', elem.name)
    assertEquals('groupchat', elem['type'])

    children = list(elem.elements())
    notification = [x for x in children if x.uri == ns.CHAT_STATES][0]
    assert notification.name == name, notification.toXml()

    if not allow_body:
        assert len(children) == 1, elem.toXml()

def test(q, bus, conn, stream):
    (muc_handle, chan, user, bob) = join_muc_and_check(q, bus, conn, stream,
        MUC)

    states = chan.Properties.Get(cs.CHANNEL_IFACE_CHAT_STATE, 'ChatStates')
    assertEquals(cs.CHAT_STATE_INACTIVE,
            states.get(user, cs.CHAT_STATE_INACTIVE))
    assertEquals(cs.CHAT_STATE_INACTIVE,
            states.get(bob, cs.CHAT_STATE_INACTIVE))

    stream.send(
        elem('message', from_=BOB, to='test@localhost/Resource',
                        type='groupchat', jid='bob@bob.bob')(
          elem(ns.CHAT_STATES, 'composing'),
          elem('google:nosave', 'x', value='disabled'),
          elem('http://jabber.org/protocol/archive', 'record', otr='false'),
        ))

    e = q.expect('dbus-signal', signal='ChatStateChanged')
    contact, state = e.args
    assertEquals(bob, contact)
    assertEquals(cs.CHAT_STATE_COMPOSING, state)

    states = chan.Properties.Get(cs.CHANNEL_IFACE_CHAT_STATE, 'ChatStates')
    assertEquals(cs.CHAT_STATE_INACTIVE,
            states.get(user, cs.CHAT_STATE_INACTIVE))
    assertEquals(cs.CHAT_STATE_COMPOSING,
            states.get(bob, cs.CHAT_STATE_INACTIVE))

    stream.send(
        elem('message', from_=BOB, to='test@localhost/Resource',
                        type='groupchat', jid='bob@bob.bob')(
          elem(ns.CHAT_STATES, 'paused'),
          elem('google:nosave', 'x', value='disabled'),
          elem('http://jabber.org/protocol/archive', 'record', otr='false'),
        ))

    e = q.expect('dbus-signal', signal='ChatStateChanged')
    contact, state = e.args
    assertEquals(bob, contact)
    assertEquals(cs.CHAT_STATE_PAUSED, state)

    states = chan.Properties.Get(cs.CHANNEL_IFACE_CHAT_STATE, 'ChatStates')
    assertEquals(cs.CHAT_STATE_INACTIVE,
            states.get(user, cs.CHAT_STATE_INACTIVE))
    assertEquals(cs.CHAT_STATE_PAUSED,
            states.get(bob, cs.CHAT_STATE_INACTIVE))

#    # Bob leaves
#    stream.send(
#        elem('presence', from_=BOB, to='test@localhost/Resource',
#                        type='unavailable'))

#    e = q.expect('dbus-signal', signal='ChatStateChanged')
#    contact, state = e.args
#    assertEquals(bob, contact)
#    assertEquals(cs.CHAT_STATE_GONE, state)

#    states = chan.Properties.Get(cs.CHANNEL_IFACE_CHAT_STATE, 'ChatStates')
#    assertEquals(cs.CHAT_STATE_INACTIVE,
#            states.get(user, cs.CHAT_STATE_INACTIVE))
#    # Bob no longer has any chat state at all
#    assertEquals(None, states.get(bob, None))

    # Sending chat states:

    # Composing...
    chan.ChatState.SetChatState(cs.CHAT_STATE_COMPOSING)

    stream_message = q.expect('stream-message')
    check_state_notification(stream_message.stanza, 'composing')

    states = chan.Properties.Get(cs.CHANNEL_IFACE_CHAT_STATE, 'ChatStates')
    assertEquals(cs.CHAT_STATE_COMPOSING,
            states.get(user, cs.CHAT_STATE_INACTIVE))

    # XEP 0085:
    #   every content message SHOULD contain an <active/> notification.
    chan.Text.Send(0, 'hi.')

    stream_message = q.expect('stream-message')
    stanza = stream_message.stanza
    check_state_notification(stanza, 'active', allow_body=True)

    states = chan.Properties.Get(cs.CHANNEL_IFACE_CHAT_STATE, 'ChatStates')
    assertEquals(cs.CHAT_STATE_ACTIVE,
            states.get(user, cs.CHAT_STATE_INACTIVE))

    bodies = list(stanza.elements(uri=ns.CLIENT, name='body'))
    assertLength(1, bodies)
    assertEquals(u'hi.', bodies[0].children[0])

if __name__ == '__main__':
      exec_test(test)
