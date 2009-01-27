
"""
Test tubes capabilities with Connection.Interface.ContactCapabilities.DRAFT

1. Receive presence and caps from contacts and check that
GetContactCapabilities works correctly and that ContactCapabilitiesChanged is
correctly received. Also check that GetContactAttributes gives the same
results.

- no tube cap at all
- 1 stream tube cap
- 1 D-Bus tube cap
- 1 stream tube + 1 D-Bus tube caps
- 2 stream tube + 2 D-Bus tube caps
- 1 stream tube + 1 D-Bus tube caps, again, to test whether the caps cache
  works with tubes

2. Test SetSelfCapabilities and test that a presence stanza is sent to the
contacts, test that the D-Bus signal ContactCapabilitiesChanged is fired for
the self handle, ask Gabble for its caps with an iq request, check the reply
is correct, and ask Gabble for its caps using D-Bus method
GetContactCapabilities. Also check that GetContactAttributes gives the same
results.

- no tube cap at all
- 1 stream tube cap
- 1 D-Bus tube cap
- 1 stream tube + 1 D-Bus tube caps
- 2 stream tube + 2 D-Bus tube caps
- 1 stream tube + 1 D-Bus tube caps, again, just for the fun

"""

import dbus
import sys

from twisted.words.xish import domish, xpath

from servicetest import EventPattern
from gabbletest import exec_test, make_result_iq, sync_stream
from constants import *

from caps_helper import compute_caps_hash
from config import PACKAGE_STRING

ns_tubes = 'http://telepathy.freedesktop.org/xmpp/tubes'

text_fixed_properties = dbus.Dictionary({
    TARGET_HANDLE_TYPE: HT_CONTACT,
    CHANNEL_TYPE: CHANNEL_TYPE_TEXT
    })
text_allowed_properties = dbus.Array([TARGET_HANDLE])

daap_fixed_properties = dbus.Dictionary({
    TARGET_HANDLE_TYPE: HT_CONTACT,
    CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
    STREAM_TUBE_SERVICE: 'daap'
    })
daap_allowed_properties = dbus.Array([TARGET_HANDLE])

http_fixed_properties = dbus.Dictionary({
    TARGET_HANDLE_TYPE: HT_CONTACT,
    CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
    STREAM_TUBE_SERVICE: 'http'
    })
http_allowed_properties = dbus.Array([TARGET_HANDLE])

xiangqi_fixed_properties = dbus.Dictionary({
    TARGET_HANDLE_TYPE: HT_CONTACT,
    CHANNEL_TYPE: CHANNEL_TYPE_DBUS_TUBE,
    DBUS_TUBE_SERVICE_NAME: 'com.example.Xiangqi'
    })
xiangqi_allowed_properties = dbus.Array([TARGET_HANDLE])

go_fixed_properties = dbus.Dictionary({
    TARGET_HANDLE_TYPE: HT_CONTACT,
    CHANNEL_TYPE: CHANNEL_TYPE_DBUS_TUBE,
    DBUS_TUBE_SERVICE_NAME: 'com.example.Go'
    })
go_allowed_properties = dbus.Array([TARGET_HANDLE])


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

def receive_presence_and_ask_caps(q, stream):
    # receive presence stanza
    event_stream, event_dbus = q.expect_many(
            EventPattern('stream-presence'),
            EventPattern('dbus-signal', signal='ContactCapabilitiesChanged')
        )
    assert len(event_dbus.args) == 1
    signaled_caps = event_dbus.args[0]

    c_nodes = xpath.queryForNodes('/presence/c', event_stream.stanza)
    assert c_nodes is not None
    assert len(c_nodes) == 1
    hash = c_nodes[0].attributes['hash']
    ver = c_nodes[0].attributes['ver']
    node = c_nodes[0].attributes['node']
    assert hash == 'sha-1'

    # ask caps
    request = """
<iq from='fake_contact@jabber.org/resource' 
    id='disco1'
    to='gabble@jabber.org/resource' 
    type='get'>
  <query xmlns='http://jabber.org/protocol/disco#info'
         node='""" + node + '#' + ver + """'/>
</iq>
"""
    stream.send(request)

    # receive caps
    event = q.expect('stream-iq',
        query_ns='http://jabber.org/protocol/disco#info')
    caps_str = str(xpath.queryForNodes('/iq/query/feature', event.stanza))

    features = []
    for feature in xpath.queryForNodes('/iq/query/feature', event.stanza):
        features.append(feature['var'])

    # Check if the hash matches the announced capabilities
    assert ver == compute_caps_hash(['client/pc//%s' % PACKAGE_STRING], features, [])

    return (event, caps_str, signaled_caps)

def caps_contain(event, cap):
    node = xpath.queryForNodes('/iq/query/feature[@var="%s"]'
            % cap,
            event.stanza)
    if node is None:
        return False
    if len(node) != 1:
        return False
    var = node[0].attributes['var']
    if var is None:
        return False
    return var == cap

def test_tube_caps_from_contact(q, bus, conn, stream, contact, contact_handle, client):

    conn_caps_iface = dbus.Interface(conn, CONN_IFACE_CONTACT_CAPA)
    conn_contacts_iface = dbus.Interface(conn, CONN_IFACE_CONTACTS)

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

    # no change in ContactCapabilities, so no signal ContactCapabilitiesChanged
    sync_stream(q, stream)

    # no special capabilities
    basic_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties)]})
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == basic_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == basic_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [CONN_IFACE_CONTACT_CAPA], False) \
            [contact_handle][CONN_IFACE_CONTACT_CAPA + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

    # send presence with 1 stream tube cap
    presence = make_presence(contact, None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = 'f5oUAlH0fcR8btEo5K0P135QReo='
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
    feature['var'] = ns_tubes + '/stream#daap'
    stream.send(result)

    # daap capabilities
    daap_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties),
             (daap_fixed_properties, daap_allowed_properties)]})
    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assert len(event.args) == 1
    assert event.args[0] == daap_caps

    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [CONN_IFACE_CONTACT_CAPA], False) \
            [contact_handle][CONN_IFACE_CONTACT_CAPA + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

    # send presence with 1 D-Bus tube cap
    presence = make_presence(contact, None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = '4Ps2iaOc+lsFwfbasCdsBjLOQ5s='
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
    feature['var'] = ns_tubes + '/dbus#com.example.Xiangqi'
    stream.send(result)

    # xiangqi capabilities
    xiangqi_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties),
            (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assert len(event.args) == 1
    assert event.args[0] == xiangqi_caps

    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == xiangqi_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [CONN_IFACE_CONTACT_CAPA], False) \
            [contact_handle][CONN_IFACE_CONTACT_CAPA + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

    # send presence with both D-Bus and stream tube caps
    presence = make_presence(contact, None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = 'ALCBfacl4M/FKWckV1OCHfj+lt0='
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
    feature['var'] = ns_tubes + '/dbus#com.example.Xiangqi'
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/stream#daap'
    stream.send(result)

    # daap + xiangqi capabilities
    daap_xiangqi_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assert len(event.args) == 1
    assert event.args[0] == daap_xiangqi_caps

    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_xiangqi_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [CONN_IFACE_CONTACT_CAPA], False) \
            [contact_handle][CONN_IFACE_CONTACT_CAPA + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

    # send presence with 4 tube caps
    presence = make_presence(contact, None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = 'ObSHJf9W0fUDuSjmB6gmthptw+s='
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
    feature['var'] = ns_tubes + '/dbus#com.example.Xiangqi'
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/dbus#com.example.Go'
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/stream#daap'
    feature = query.addElement('feature')
    feature['var'] = ns_tubes + '/stream#http'
    stream.send(result)

    # http + daap + xiangqi + go capabilities
    all_tubes_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (http_fixed_properties, http_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties),
        (go_fixed_properties, go_allowed_properties)]})
    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assert len(event.args) == 1
    assert event.args[0] == all_tubes_caps

    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == all_tubes_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == all_tubes_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [CONN_IFACE_CONTACT_CAPA], False) \
            [contact_handle][CONN_IFACE_CONTACT_CAPA + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

    # send presence with both D-Bus and stream tube caps
    presence = make_presence(contact, None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = 'ALCBfacl4M/FKWckV1OCHfj+lt0='
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble does not look up our capabilities because of the cache

    # daap + xiangqi capabilities
    daap_xiangqi_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assert len(event.args) == 1
    assert event.args[0] == daap_xiangqi_caps

    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_xiangqi_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [CONN_IFACE_CONTACT_CAPA], False) \
            [contact_handle][CONN_IFACE_CONTACT_CAPA + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

def test_tube_caps_to_contact(q, bus, conn, stream):
    basic_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties)]})
    daap_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties)]})
    xiangqi_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    daap_xiangqi_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    all_tubes_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (http_fixed_properties, http_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties),
        (go_fixed_properties, go_allowed_properties)]})

    conn_caps_iface = dbus.Interface(conn, CONN_IFACE_CONTACT_CAPA)
    conn_contacts_iface = dbus.Interface(conn, CONN_IFACE_CONTACTS)

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == basic_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [CONN_IFACE_CONTACT_CAPA], False) \
            [1][CONN_IFACE_CONTACT_CAPA + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise nothing
    conn_caps_iface.SetSelfCapabilities([])

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert len(caps) == 1
    assert caps == basic_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [CONN_IFACE_CONTACT_CAPA], False) \
            [1][CONN_IFACE_CONTACT_CAPA + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    sync_stream(q, stream)

    # Advertise daap
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [daap_fixed_properties])

    # Expect Gabble to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, stream)
    assert caps_contain(event, ns_tubes) == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#http') == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Xiangqi') \
            == False, caps_str
    assert signaled_caps == daap_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == daap_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [CONN_IFACE_CONTACT_CAPA], False) \
            [1][CONN_IFACE_CONTACT_CAPA + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise xiangqi
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [xiangqi_fixed_properties])

    # Expect Gabble to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, stream)
    assert caps_contain(event, ns_tubes) == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#daap') == False, caps_str
    assert caps_contain(event, ns_tubes + '/stream#http') == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert signaled_caps == xiangqi_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [CONN_IFACE_CONTACT_CAPA], False) \
            [1][CONN_IFACE_CONTACT_CAPA + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise daap + xiangqi
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [daap_fixed_properties, xiangqi_fixed_properties])

    # Expect Gabble to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, stream)
    assert caps_contain(event, ns_tubes) == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#http') == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert signaled_caps == daap_xiangqi_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == daap_xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [CONN_IFACE_CONTACT_CAPA], False) \
            [1][CONN_IFACE_CONTACT_CAPA + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise 4 tubes
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [daap_fixed_properties, http_fixed_properties,
         go_fixed_properties, xiangqi_fixed_properties])

    # Expect Gabble to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, stream)
    assert caps_contain(event, ns_tubes) == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#http') == True, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Go') \
            == True, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert signaled_caps == all_tubes_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == all_tubes_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [CONN_IFACE_CONTACT_CAPA], False) \
            [1][CONN_IFACE_CONTACT_CAPA + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise daap + xiangqi
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [daap_fixed_properties, xiangqi_fixed_properties])

    # Expect Gabble to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, stream)
    assert caps_contain(event, ns_tubes) == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns_tubes + '/stream#http') == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns_tubes + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert signaled_caps == daap_xiangqi_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == daap_xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [CONN_IFACE_CONTACT_CAPA], False) \
            [1][CONN_IFACE_CONTACT_CAPA + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface


def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    client = 'http://telepathy.freedesktop.org/fake-client'

    test_tube_caps_from_contact(q, bus, conn, stream, 'bilbo1@foo.com/Foo',
        2L, client)

    test_tube_caps_to_contact(q, bus, conn, stream)

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])


if __name__ == '__main__':
    exec_test(test)

