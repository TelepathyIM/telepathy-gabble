
"""
Test that updating an alias saves it to the roster.
"""

import dbus

from servicetest import EventPattern, call_async, assertEquals
from gabbletest import (
    acknowledge_iq, exec_test, make_result_iq, sync_stream, elem
    )
import constants as cs
import ns
from rostertest import expect_contact_list_signals, send_roster_push
from pubsub import make_pubsub_event

def send_pep_nick_reply(stream, stanza, nickname):
    result = make_result_iq(stream, stanza)
    pubsub = result.firstChildElement()
    items = pubsub.addElement('items')
    items['node'] = ns.NICK
    item = items.addElement('item')
    item.addElement('nick', ns.NICK, content=nickname)
    stream.send(result)

def check_roster_write(stream, event, jid, name):
    item = event.query.firstChildElement()
    assertEquals(jid, item['jid'])
    # This copes with name=None
    assertEquals(name, item.getAttribute('name'))

    acknowledge_iq(stream, event.stanza)
    # RFC 3921 requires the server to send a roster push to all connected
    # resources whenever a resource updates the roster. Gabble depends on this
    # and pays no attention to its own nick update until the server sends a
    # push.
    send_roster_push(stream, jid, 'none', name=name)

def expect_AliasesChanged_and_roster_write(q, stream, handle, jid, nick):
    roster_write = q.expect('stream-iq', iq_type='set', query_ns=ns.ROSTER)
    check_roster_write(stream, roster_write, jid, nick)

    q.expect('dbus-signal', signal='AliasesChanged',
        args=[[(handle, nick if nick else jid)]])

def test(q, bus, conn, stream):
    event, event2 = q.expect_many(
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', query_ns=ns.ROSTER))

    acknowledge_iq(stream, event.stanza)
    acknowledge_iq(stream, event2.stanza)

    signals = expect_contact_list_signals(q, bus, conn, lists=['subscribe'])
    old_signal, new_signal = signals[0]
    path = old_signal.args[0]

    # request subscription
    chan = bus.get_object(conn.bus_name, path)
    group_iface = dbus.Interface(chan, cs.CHANNEL_IFACE_GROUP)
    assert group_iface.GetMembers() == []
    handle = conn.get_contact_handle_sync('bob@foo.com')
    call_async(q, group_iface, 'AddMembers', [handle], '')

    event = q.expect('stream-iq', iq_type='set', query_ns=ns.ROSTER)
    item = event.query.firstChildElement()

    acknowledge_iq(stream, event.stanza)
    q.expect('dbus-return', method='AddMembers')

    call_async(q, conn.Aliasing, 'RequestAliases', [handle])

    event = q.expect('stream-iq', iq_type='get',
        query_ns='http://jabber.org/protocol/pubsub',
        to='bob@foo.com')
    send_pep_nick_reply(stream, event.stanza, 'Bobby')

    event, _ = q.expect_many(
        EventPattern('stream-iq', iq_type='set', query_ns=ns.ROSTER),
        EventPattern('dbus-return', method='RequestAliases',
        value=(['Bobby'],)))
    check_roster_write(stream, event, 'bob@foo.com', 'Bobby')

    # We get a roster push for a contact who for some reason has their alias
    # set on our roster to the empty string (maybe a buggy client?). It's never
    # useful for Gabble to say that someone's alias is the empty string (given
    # the current semantics where the alias is always meant to be something you
    # could show, even if it's just their JID), so let's forbid that.
    jid = 'parts@labor.lit'
    handle = conn.get_contact_handle_sync(jid)
    q.forbid_events([EventPattern('dbus-signal', signal='AliasesChanged',
        args=[[(handle, '')]])])

    send_roster_push(stream, jid, 'both', name='')
    # I don't really have very strong opinions on whether Gabble should be
    # signalling that this contact's alias has *changed* per se, so am not
    # explicitly expecting that.
    q.expect('dbus-signal', signal='MembersChanged')

    # But if we ask for it, Gabble should probably send a PEP query.
    assertEquals(jid, conn.Aliasing.GetAliases([handle])[handle])
    event = q.expect('stream-iq', iq_type='get', query_ns=ns.PUBSUB, to=jid)
    nick = 'Constant Future'

    send_pep_nick_reply(stream, event.stanza, nick)
    expect_AliasesChanged_and_roster_write(q, stream, handle, jid, nick)

    # Here's another contact, whose alias is set on our roster to their JID:
    # because we've cached that they have no alias. Gabble shouldn't make
    # unsolicited PEP or vCard queries to them.
    jid = 'friendly@faith.plate'
    handle = conn.get_contact_handle_sync(jid)

    q.forbid_events([
        EventPattern('stream-iq', query_ns=ns.PUBSUB, to=jid),
        EventPattern('stream-iq', query_ns=ns.VCARD_TEMP, to=jid),
    ])
    send_roster_push(stream, jid, 'both', name=jid)
    q.expect('dbus-signal', signal='AliasesChanged', args=[[(handle, jid)]])
    sync_stream(q, stream)

    # But if we get a PEP nickname update for this contact, Gabble should use
    # the new nickname, and write it back to the roster.
    nick = u'The Friendly Faith Plate'
    stream.send(make_pubsub_event(jid, ns.NICK, elem(ns.NICK, 'nick')(nick)))
    expect_AliasesChanged_and_roster_write(q, stream, handle, jid, nick)

    # As an undocumented extension, we treat setting the alias to the empty
    # string to mean "whatever the contact says their nickname is". (The rest
    # of this test is a regression test for
    # <https://bugs.freedesktop.org/show_bug.cgi?id=11321>.)
    #
    # So first up, let's change the Friendly Faith Plate's nickname to
    # something else.
    custom_nick = u'I saw a deer today'
    conn.Aliasing.SetAliases({handle: custom_nick})
    expect_AliasesChanged_and_roster_write(q, stream, handle, jid, custom_nick)

    assertEquals([custom_nick], conn.Aliasing.RequestAliases([handle]))

    # And now set it to the empty string. Since Gabble happens to have a
    # nickname this contact specified cached, it should switch over to that one.
    conn.Aliasing.SetAliases({handle: ''})
    expect_AliasesChanged_and_roster_write(q, stream, handle, jid, nick)
    assertEquals([nick], conn.Aliasing.RequestAliases([handle]))

    # Here's a contact we haven't seen before, pushed to our roster with a
    # nickname already there.
    jid = 'glados@aperture.lit'
    handle = conn.get_contact_handle_sync(jid)
    nick = 'Potato'

    send_roster_push(stream, jid, 'both', name=nick)
    q.expect('dbus-signal', signal='AliasesChanged', args=[[(handle, nick)]])

    # If the user clears their alias, we should expect Gabble to say over D-Bus
    # that their nickname is their jid, and send a roster push removing the
    # name='' attribute...
    conn.Aliasing.SetAliases({handle: ''})
    expect_AliasesChanged_and_roster_write(q, stream, handle, jid, None)

    # ...and also send a PEP query to find a better nickname; when the contact
    # replies, Gabble should update the roster accordingly.
    event = q.expect('stream-iq', iq_type='get', query_ns=ns.PUBSUB, to=jid)
    send_pep_nick_reply(stream, event.stanza, 'GLaDOS')
    expect_AliasesChanged_and_roster_write(q, stream, handle, jid, 'GLaDOS')

if __name__ == '__main__':
    exec_test(test)
