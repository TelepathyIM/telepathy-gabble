"""
Regression test for <https://bugs.freedesktop.org/show_bug.cgi?id=32952>,
wherein chat states in MUCs were misparsed.
"""

from servicetest import assertEquals
from gabbletest import exec_test, elem
from mucutil import join_muc_and_check
import ns
import constants as cs

MUC = 'ohai@groupchat.google.com'
BOB = MUC + '/bob'

def test(q, bus, conn, stream):
    (muc_handle, chan, user, bob) = join_muc_and_check(q, bus, conn, stream,
        MUC)

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

if __name__ == '__main__':
      exec_test(test)
