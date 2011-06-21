
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

def check_roster_write(event, jid, name):
    item = event.query.firstChildElement()
    assertEquals(jid, item['jid'])
    assertEquals(name, item['name'])

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
    handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
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
    check_roster_write(event, 'bob@foo.com', 'Bobby')

    # We get a roster push for a contact who for some reason has their alias
    # set on our roster to the empty string (maybe a buggy client?). It's never
    # useful for Gabble to say that someone's alias is the empty string (given
    # the current semantics where the alias is always meant to be something you
    # could show, even if it's just their JID), so let's forbid that.
    jid = 'parts@labor.lit'
    handle = conn.RequestHandles(cs.HT_CONTACT, [jid])[0]
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
    _, roster_write = q.expect_many(
        EventPattern('dbus-signal', signal='AliasesChanged',
            args=[[(handle, nick)]]),
        EventPattern('stream-iq', iq_type='set', query_ns=ns.ROSTER),
        )
    check_roster_write(roster_write, jid, nick)

    # Here's another contact, whose alias is set on our roster to their JID:
    # because we've cached that they have no alias. Gabble shouldn't make
    # unsolicited PEP or vCard queries to them.
    jid = 'friendly@faith.plate'
    handle = conn.RequestHandles(cs.HT_CONTACT, [jid])[0]

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
    _, roster_write = q.expect_many(
        EventPattern('dbus-signal', signal='AliasesChanged',
            args=[[(handle, nick)]]),
        EventPattern('stream-iq', iq_type='set', query_ns=ns.ROSTER),
        )
    check_roster_write(roster_write, jid, nick)

if __name__ == '__main__':
    exec_test(test)
