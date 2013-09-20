"""
Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=27913 where
scrollback messages from contacts not currently in the MUC don't show up
in the MUC; instead, they seemed to show up on IM channels whose target is the
MUC's bare JID.

Also acts as a scrollback messages in general!
"""

import dbus

from servicetest import assertEquals, assertContains
from gabbletest import exec_test, elem

import constants as cs
import ns

from mucutil import join_muc_and_check

def test(q, bus, conn, stream):
    room = 'chat@conf.localhost'
    our_jid = room + '/test'
    bob_jid = room + '/bob'
    marco_jid = room + '/marco'

    room_handle, chan, test_handle, bob_handle = \
        join_muc_and_check(q, bus, conn, stream, room)

    # Here are a few scrollback messages. One from us; one from bob; and one
    # from marco, who's no longer in the room.
    stream.send(
        elem('message', from_=our_jid, type='groupchat')(
          elem('body')(
            u'i really hate the muc xep'
          ),
          elem(ns.X_DELAY, 'x', from_=room, stamp='20090910T12:34:56')
        )
      )
    stream.send(
        elem('message', from_=bob_jid, type='groupchat')(
          elem('body')(
            u'yeah, it totally sucks'
          ),
          elem(ns.X_DELAY, 'x', from_=room, stamp='20090910T12:45:56')
        )
      )
    stream.send(
        elem('message', from_=marco_jid, type='groupchat')(
          elem('body')(
            u'we should start a riot'
          ),
          elem(ns.X_DELAY, 'x', from_=room, stamp='20090910T12:56:56')
        )
      )

    m1 = q.expect('dbus-signal', signal='MessageReceived')
    m2 = q.expect('dbus-signal', signal='MessageReceived')
    m3 = q.expect('dbus-signal', signal='MessageReceived')

    def badger(event):
        assertEquals(chan.object_path, event.path)

        message, = event.args
        header = message[0]

        assertContains('scrollback', header)
        assert header['scrollback']

        assertContains('message-sender', header)
        return header['message-sender']

    me = badger(m1)
    bob = badger(m2)
    marco = badger(m3)

    assertEquals([our_jid, bob_jid, marco_jid],
        conn.inspect_contacts_sync([ me, bob, marco ]))

if __name__ == '__main__':
    exec_test(test)
