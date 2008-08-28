
"""
Test tubes capabilities with Connection.Interface.ContactCapabilities.DRAFT
Receive presence and caps from contacts and check that GetContactCapabilities
works correctly. Test:
- no tube cap at all
- one stream tube cap
- one D-Bus tube cap
- several tube caps
TODO: signals
"""

import dbus
import sys

from twisted.words.xish import domish, xpath

from servicetest import EventPattern
from gabbletest import exec_test, make_result_iq, sync_stream

text_iface = 'org.freedesktop.Telepathy.Channel.Type.Text'
caps_iface = 'org.freedesktop.Telepathy.' + \
             'Connection.Interface.ContactCapabilities.DRAFT'

ns_tubes = 'http://telepathy.freedesktop.org/xmpp/tubes'

text_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.Text'
    })
text_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])

daap_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT',
    'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service':
        'daap'
    })
daap_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])

http_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT',
    'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service':
        'http'
    })
http_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])

xiangqi_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT',
    'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName':
        'com.example.Xiangqi'
    })
xiangqi_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])

go_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT',
    'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName':
        'com.example.Go'
    })
go_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])


def make_presence(from_jid, type, status):
    presence = domish.Element((None, 'presence'))

    if from_jid is not None:
        presence['from'] = from_jid

    if type is not None:
        presence['type'] = type

    if status is not None:
        presence.addElement('status', content=status)

    return presence

def presence_add_caps(presence, ver, client, hash=None):
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = ver
    if hash is not None:
        c['hash'] = hash
    return presence

def _test_tube_caps(q, bus, conn, stream, contact, contact_handle, client):

    conn_caps_iface = dbus.Interface(conn, caps_iface)

    # send presence with no tube cap
    presence = make_presence(contact, None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = 'JpaYgiKL0y4fUOCTwN3WLGpaftM='
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ver']

    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle'
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle/description/audio'
    feature = query.addElement('feature')
    feature['var'] = 'http://www.google.com/transport/p2p'
    stream.send(result)
    sync_stream(q, stream)

    #event = q.expect('dbus-signal', signal='CapabilitiesChanged')

    # no special capabilities
    basic_caps = [(contact_handle, text_fixed_properties,
            text_allowed_properties)]
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == basic_caps, caps

    # send presence with 1 stream tube cap
    presence = make_presence(contact, None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = 'njTWnNVMGeDjS8+4TkMuMX6Z/Ug='
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ver']
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/stream/daap'
    stream.send(result)
    sync_stream(q, stream)

    #event = q.expect('dbus-signal', signal='CapabilitiesChanged')

    # daap capabilities
    daap_caps = [
        (contact_handle, text_fixed_properties, text_allowed_properties),
        (contact_handle, daap_fixed_properties, daap_allowed_properties)]
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_caps, caps

    # send presence with 1 D-Bus tube cap
    presence = make_presence(contact, None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = '8/mwj7yF0K23YT6GurBXI1X4hd4='
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ver']
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/dbus/com.example.Xiangqi'
    stream.send(result)
    sync_stream(q, stream)

    #event = q.expect('dbus-signal', signal='CapabilitiesChanged')

    # xiangqi capabilities
    xiangqi_caps = [
        (contact_handle, text_fixed_properties, text_allowed_properties),
        (contact_handle, xiangqi_fixed_properties, xiangqi_allowed_properties)]
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == xiangqi_caps, caps

    # send presence with both D-Bus and stream tube caps
    presence = make_presence(contact, None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = 'moS31cvk2kf9Zka4gb6ncj2VJCo='
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ver']
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/dbus/com.example.Xiangqi'
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/stream/daap'
    stream.send(result)
    sync_stream(q, stream)

    #event = q.expect('dbus-signal', signal='CapabilitiesChanged')

    # daap + xiangqi capabilities
    daap_xiangqi_caps = [
        (contact_handle, text_fixed_properties, text_allowed_properties),
        (contact_handle, daap_fixed_properties, daap_allowed_properties),
        (contact_handle, xiangqi_fixed_properties, xiangqi_allowed_properties)]
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_xiangqi_caps, caps

    # send presence with 4 tube caps
    presence = make_presence(contact, None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = '4uwiaJY110AjLEFSIeu4/mVJ8wc='
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ver']
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/dbus/com.example.Xiangqi'
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/dbus/com.example.Go'
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/stream/daap'
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/stream/http'
    stream.send(result)
    sync_stream(q, stream)

    #event = q.expect('dbus-signal', signal='CapabilitiesChanged')

    # http + daap + xiangqi + go capabilities
    all_tubes_caps = [
        (contact_handle, text_fixed_properties, text_allowed_properties),
        (contact_handle, daap_fixed_properties, daap_allowed_properties),
        (contact_handle, http_fixed_properties, http_allowed_properties),
        (contact_handle, xiangqi_fixed_properties,
                xiangqi_allowed_properties),
        (contact_handle, go_fixed_properties, go_allowed_properties)]
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == all_tubes_caps, caps


def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    client = 'http://telepathy.freedesktop.org/fake-client'

    _test_tube_caps(q, bus, conn, stream, 'bilbo1@foo.com/Foo', 2L, client)

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])


if __name__ == '__main__':
    exec_test(test)

