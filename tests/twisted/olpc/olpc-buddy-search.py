"""
test OLPC search buddy
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq

from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ

NS_OLPC_BUDDY_PROPS = "http://laptop.org/xmpp/buddy-properties"
NS_OLPC_ACTIVITIES = "http://laptop.org/xmpp/activities"
NS_OLPC_CURRENT_ACTIVITY = "http://laptop.org/xmpp/current-activity"
NS_OLPC_ACTIVITY_PROPS = "http://laptop.org/xmpp/activity-properties"
NS_OLPC_BUDDY = "http://laptop.org/xmpp/buddy"
NS_OLPC_ACTIVITY = "http://laptop.org/xmpp/activity"

NS_PUBSUB = "http://jabber.org/protocol/pubsub"
NS_DISCO_INFO = "http://jabber.org/protocol/disco#info"
NS_DISCO_ITEMS = "http://jabber.org/protocol/disco#items"


NS_AMP = "http://jabber.org/protocol/amp"

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=NS_DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)

    # announce Gadget service
    reply = make_result_iq(stream, disco_event.stanza)
    query = xpath.queryForNodes('/iq/query', reply)[0]
    item = query.addElement((None, 'item'))
    item['jid'] = 'gadget.localhost'
    stream.send(reply)

    # wait for Gadget disco#info query
    event = q.expect('stream-iq', to='gadget.localhost', query_ns=NS_DISCO_INFO)
    reply = make_result_iq(stream, event.stanza)
    query = xpath.queryForNodes('/iq/query', reply)[0]
    identity = query.addElement((None, 'identity'))
    identity['category'] = 'collaboration'
    identity['type'] = 'gadget'
    identity['name'] = 'OLPC Gadget'
    feature = query.addElement((None, 'feature'))
    feature['var'] = NS_OLPC_BUDDY
    feature = query.addElement((None, 'feature'))
    feature['var'] = NS_OLPC_ACTIVITY
    stream.send(reply)

    buddy_info_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    buddy_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Buddy')

    call_async(q, conn, 'RequestHandles', 1, ['bob@localhost'])

    event = q.expect('dbus-return', method='RequestHandles')
    handles = event.value[0]
    bob_handle = handles[0]

    call_async(q, buddy_info_iface, 'GetProperties', bob_handle)

    # wait for pubsub query
    event = q.expect('stream-iq', to='bob@localhost', query_ns=NS_PUBSUB)
    query = event.stanza
    assert query['to'] == 'bob@localhost'

    # send an error as reply
    reply = IQ(stream, 'error')
    reply['id'] = query['id']
    reply['to'] = 'alice@localhost'
    reply['from'] = 'bob@localhost'
    stream.send(reply)

    # wait for buddy search query
    event = q.expect('stream-iq', to='gadget.localhost',
            query_ns=NS_OLPC_BUDDY)
    buddies = xpath.queryForNodes('/iq/query/buddy', event.stanza)
    assert len(buddies) == 1
    buddy = buddies[0]
    assert buddy['jid'] == 'bob@localhost'

    # send reply to the search query
    reply = make_result_iq(stream, event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    query = xpath.queryForNodes('/iq/query', reply)[0]
    buddy = query.addElement((None, "buddy"))
    buddy['jid'] = 'bob@localhost'
    properties = buddy.addElement((NS_OLPC_BUDDY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#005FE4,#00A0FF')
    stream.send(reply)

    event = q.expect('dbus-return', method='GetProperties')
    props = event.value[0]

    assert props == {'color': '#005FE4,#00A0FF' }

    # request 3 random buddies
    call_async(q, buddy_iface, 'RequestRandom', 3)

    iq_event, return_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost', query_ns=NS_OLPC_BUDDY),
        EventPattern('dbus-return', method='RequestRandom'))

    query = iq_event.stanza.firstChildElement()
    assert query.name == 'query'
    assert query['id'] == '0'
    random = xpath.queryForNodes('/iq/query/random', iq_event.stanza)
    assert len(random) == 1
    assert random[0]['max'] == '3'

    # reply to random query
    reply = make_result_iq(stream, iq_event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    query = xpath.queryForNodes('/iq/query', reply)[0]
    buddy = query.addElement((None, "buddy"))
    buddy['jid'] = 'bob@localhost'
    properties = buddy.addElement((NS_OLPC_BUDDY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#005FE4,#00A0FF')
    stream.send(reply)

    view_path = return_event.value[0]
    view0 = bus.get_object(conn.bus_name, view_path)
    view0_iface = dbus.Interface(view0, 'org.laptop.Telepathy.BuddyView')
    view0_group_iface = dbus.Interface(view0, 'org.freedesktop.Telepathy.Channel.Interface.Group')

    event = q.expect('dbus-signal', signal='PropertiesChanged')
    handle, props = event.args
    assert conn.InspectHandles(1, [handle])[0] == 'bob@localhost'
    assert props == {'color': '#005FE4,#00A0FF'}

    event = q.expect('dbus-signal', signal='MembersChanged')
    msg, added, removed, lp, rp, actor, reason = event.args
    assert (removed, lp, rp) == ([], [], [])
    assert len(added) == 1
    handle = added[0]
    assert conn.InspectHandles(1, [handle])[0] == 'bob@localhost'

    # buddy search
    props = {'color': '#AABBCC,#001122'}
    call_async(q, buddy_iface, 'SearchByProperties', props)

    iq_event, return_event = q.expect_many(
        EventPattern('stream-iq', to='gadget.localhost', query_ns=NS_OLPC_BUDDY),
        EventPattern('dbus-return', method='SearchByProperties'))

    properties = xpath.queryForNodes('/iq/query/buddy/properties/property', iq_event.stanza)
    query = iq_event.stanza.firstChildElement()
    assert query.name == 'query'
    assert query['id'] == '1'
    assert len(properties) == 1
    property = properties[0]
    assert property['type'] == 'str'
    assert property['name'] == 'color'
    assert property.children == ['#AABBCC,#001122']

    # reply to request
    reply = make_result_iq(stream, iq_event.stanza)
    reply['from'] = 'gadget.localhost'
    reply['to'] = 'alice@localhost'
    query = xpath.queryForNodes('/iq/query', reply)[0]
    buddy = query.addElement((None, "buddy"))
    buddy['jid'] = 'charles@localhost'
    properties = buddy.addElement((NS_OLPC_BUDDY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#AABBCC,#001122')
    stream.send(reply)

    view_path = return_event.value[0]
    view1 = bus.get_object(conn.bus_name, view_path)
    view1_iface = dbus.Interface(view1, 'org.laptop.Telepathy.BuddyView')

    event = q.expect('dbus-signal', signal='PropertiesChanged')
    handle, props = event.args
    assert conn.InspectHandles(1, [handle])[0] == 'charles@localhost'
    assert props == {'color': '#AABBCC,#001122'}

    event = q.expect('dbus-signal', signal='MembersChanged')
    msg, added, removed, lp, rp, actor, reason = event.args
    assert (removed, lp, rp) == ([], [], [])
    assert len(added) == 1
    handle = added[0]
    assert conn.InspectHandles(1, [handle])[0] == 'charles@localhost'

    # add a buddy to view 0
    message = domish.Element((None, 'message'))
    message['from'] = 'gadget.localhost'
    message['to'] = 'alice@localhost'
    message['type'] = 'notice'
    added = message.addElement((NS_OLPC_BUDDY, 'added'))
    added['id'] = '0'
    buddy = added.addElement((None, 'buddy'))
    buddy['jid'] = 'oscar@localhost'
    properties = buddy.addElement((NS_OLPC_BUDDY_PROPS, "properties"))
    property = properties.addElement((None, "property"))
    property['type'] = 'str'
    property['name'] = 'color'
    property.addContent('#000000,#AAAAAA')
    amp = message.addElement((NS_AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'
    stream.send(message)

    event = q.expect('dbus-signal', signal='MembersChanged')
    msg, added, removed, lp, rp, actor, reason = event.args
    assert (removed, lp, rp) == ([], [], [])
    assert len(added) == 1
    handle = added[0]
    assert conn.InspectHandles(1, [handle])[0] == 'oscar@localhost'

    members = view0_group_iface.GetMembers()
    members = sorted(conn.InspectHandles(1, members))
    assert members == ['bob@localhost', 'oscar@localhost']

    # remove a buddy from view 0
    message = domish.Element((None, 'message'))
    message['from'] = 'gadget.localhost'
    message['to'] = 'alice@localhost'
    message['type'] = 'notice'
    added = message.addElement((NS_OLPC_BUDDY, 'removed'))
    added['id'] = '0'
    buddy = added.addElement((None, 'buddy'))
    buddy['jid'] = 'bob@localhost'
    amp = message.addElement((NS_AMP, 'amp'))
    rule = amp.addElement((None, 'rule'))
    rule['condition'] = 'deliver-at'
    rule['value'] = 'stored'
    rule['action'] ='error'
    stream.send(message)

    event = q.expect('dbus-signal', signal='MembersChanged')
    msg, added, removed, lp, rp, actor, reason = event.args
    assert (added, lp, rp) == ([], [], [])
    assert len(removed) == 1
    handle = removed[0]
    assert conn.InspectHandles(1, [handle])[0] == 'bob@localhost'

    members = view0_group_iface.GetMembers()
    members = sorted(conn.InspectHandles(1, members))
    assert members == ['oscar@localhost']

    # close view 0
    call_async(q, view0_iface, 'Close')
    event, _ = q.expect_many(
        EventPattern('stream-message', to='gadget.localhost'),
        EventPattern('dbus-return', method='Close'))
    close = xpath.queryForNodes('/message/close', event.stanza)
    assert len(close) == 1
    assert close[0]['id'] == '0'

    # close view 1
    call_async(q, view1_iface, 'Close')
    event, _ = q.expect_many(
        EventPattern('stream-message', to='gadget.localhost'),
        EventPattern('dbus-return', method='Close'))
    close = xpath.queryForNodes('/message/close', event.stanza)
    assert len(close) == 1
    assert close[0]['id'] == '1'

if __name__ == '__main__':
    exec_test(test)
