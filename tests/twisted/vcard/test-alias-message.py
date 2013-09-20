# vim: set fileencoding=utf-8 :
"""
Tests grabbing aliases from incoming messages, as described by
<http://xmpp.org/extensions/xep-0172.html#message>.

This test has nothing to do with vcards, just like a lot of other tests in the
vcard/ directory.
"""

from servicetest import EventPattern, assertEquals, wrap_channel
from gabbletest import exec_test, elem, expect_and_handle_get_vcard
from mucutil import join_muc, make_muc_presence

import constants as cs
import ns

def test(q, bus, conn, stream):
    expect_and_handle_get_vcard(q, stream)

    jid = u'bora.horza.gobuchul@culture.lit'
    alias = u'Horza'
    handle = conn.get_contact_handle_sync(jid)

    # We don't have an interesting alias for Horza
    assertEquals({handle: jid}, conn.Aliasing.GetAliases([handle]))

    # Horza sends us a message containing his preferred nickname.
    stream.send(
        elem('message', from_=jid, type='chat')(
          elem('body')(u"It's a long story."),
          elem(ns.NICK, 'nick')(alias)
        ))
    _, mr = q.expect_many(
        EventPattern('dbus-signal', signal='AliasesChanged',
            args=[[(handle, alias)]]),
        EventPattern('dbus-signal', signal='MessageReceived'),
        )

    channel = wrap_channel(bus.get_object(conn.bus_name, mr.path), 'Text')

    # So now we know his alias.
    assertEquals({handle: alias}, conn.Aliasing.GetAliases([handle]))

    # Presumably to avoid non-contacts being able to make Gabble's memory
    # footprint grow forever, Gabble throws the alias away when we close the
    # channel.
    header = mr.args[0][0]
    channel.Text.AcknowledgePendingMessages([header['pending-message-id']])
    channel.Close()

    # FIXME: Gabble forgets the alias, but it doesn't signal that it has done
    # so; it probably should.
    # q.expect('dbus-signal', signal='AliasesChanged', args=[[(handle, jid)]])
    assertEquals({handle: jid}, conn.Aliasing.GetAliases([handle]))


    # Basically the same test, but in a MUC.
    #
    # It's a bit questionable whether this ought to work.
    # <http://xmpp.org/extensions/xep-0172.html#muc> doesn't have anything to
    # say about including <nick/> in messages; it does talk about including
    # <nick> in your MUC presence, which is actually equally sketchy! If I join
    # a muc with the resource '/wjt', and you join with resource '/ohai' but
    # say that your nickname is 'wjt', what on earth is Alice's UI supposed to
    # show when you send a message?
    #
    # But anyway, at the time of writing this "works", so I'm adding a test to
    # make it explicit.  Perhaps in future we might change this test to verify
    # that it doesn't "work".
    room_jid = 'clear-air-turbulence@culture.lit'
    _, muc, _, _ = join_muc(q, bus, conn, stream, room_jid)

    bob_jid = room_jid + '/bob'
    bob_handle = conn.get_contact_handle_sync(bob_jid)

    assertEquals({bob_handle: 'bob'}, conn.Aliasing.GetAliases([bob_handle]))

    stream.send(
        elem('message', from_=bob_jid, type='groupchat')(
          elem('body')(u'My religion dies with me.'),
          elem(ns.NICK, 'nick')(alias),
        ))

    q.expect_many(
        EventPattern('dbus-signal', signal='AliasesChanged',
            args=[[(bob_handle, alias)]]),
        EventPattern('dbus-signal', signal='MessageReceived'),)

    assertEquals({bob_handle: alias}, conn.Aliasing.GetAliases([bob_handle]))

    muc.Close()
    q.expect('stream-presence', to=room_jid + '/test')
    echo = make_muc_presence('member', 'none', room_jid, 'test')
    echo['type'] = 'unavailable'
    stream.send(echo)
    q.expect('dbus-signal', signal='ChannelClosed')

    # FIXME: Gabble forgets the alias, but it doesn't signal that it has done
    # so; it probably should.
    # q.expect('dbus-signal', signal='AliasesChanged',
    #     args=[[(bob_handle, 'bob')]])
    assertEquals({bob_handle: 'bob'}, conn.Aliasing.GetAliases([bob_handle]))

if __name__ == '__main__':
    exec_test(test)
