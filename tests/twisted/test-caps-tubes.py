
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

from twisted.words.xish import xpath

from gabbletest import exec_test, make_result_iq, sync_stream, make_presence
import constants as cs

from caps_helper import compute_caps_hash, text_fixed_properties,\
    text_allowed_properties, stream_tube_fixed_properties, stream_tube_allowed_properties,\
    dbus_tube_fixed_properties, dbus_tube_allowed_properties, receive_presence_and_ask_caps,\
    caps_contain, ft_fixed_properties, ft_allowed_properties
import ns

specialized_tube_allowed_properties = dbus.Array([cs.TARGET_HANDLE,
    cs.TARGET_ID])

daap_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
    cs.STREAM_TUBE_SERVICE: 'daap'
    })
http_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
    cs.STREAM_TUBE_SERVICE: 'http'
    })

xiangqi_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
    cs.DBUS_TUBE_SERVICE_NAME: 'com.example.Xiangqi'
    })

go_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
    cs.DBUS_TUBE_SERVICE_NAME: 'com.example.Go'
    })

def test_tube_caps_from_contact(q, bus, conn, stream, contact, contact_handle, client):

    conn_caps_iface = dbus.Interface(conn, cs.CONN_IFACE_CONTACT_CAPS)
    conn_contacts_iface = dbus.Interface(conn, cs.CONN_IFACE_CONTACTS)

    # send presence with no tube cap
    presence = make_presence(contact, status='hello')
    c = presence.addElement((ns.CAPS, 'c'))
    c['node'] = client
    c['ver'] = compute_caps_hash([], [], [])
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact, query_ns=ns.DISCO_INFO)
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ver']
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
            [contact_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][cs.CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

    # send presence with generic tubes caps
    presence = make_presence(contact, status='hello')
    c = presence.addElement((ns.CAPS, 'c'))
    c['node'] = client
    c['ver'] = compute_caps_hash([], [ns.TUBES], [])
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact, query_ns=ns.DISCO_INFO)
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ver']
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES
    stream.send(result)

    # generic tubes capabilities
    generic_tubes_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties),
             (stream_tube_fixed_properties, stream_tube_allowed_properties),
             (dbus_tube_fixed_properties, dbus_tube_allowed_properties)]})
    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assert len(event.args) == 1
    assert event.args[0] == generic_tubes_caps

    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == generic_tubes_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == generic_tubes_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][cs.CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface


    # send presence with 1 stream tube cap
    presence = make_presence(contact, status='hello')
    c = presence.addElement((ns.CAPS, 'c'))
    c['node'] = client
    c['ver'] = compute_caps_hash([], [ns.TUBES + '/stream#daap'], [])
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact, query_ns=ns.DISCO_INFO)
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ver']
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/stream#daap'
    stream.send(result)

    # daap capabilities
    daap_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties),
             (stream_tube_fixed_properties, stream_tube_allowed_properties),
             (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
             (daap_fixed_properties, specialized_tube_allowed_properties)]})
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
            [contact_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][cs.CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

    # send presence with 1 D-Bus tube cap
    presence = make_presence(contact, status='hello')
    c = presence.addElement((ns.CAPS, 'c'))
    c['node'] = client
    c['ver'] = compute_caps_hash([], [ns.TUBES + '/dbus#com.example.Xiangqi'], [])
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact, query_ns=ns.DISCO_INFO)
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ver']
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/dbus#com.example.Xiangqi'
    stream.send(result)

    # xiangqi capabilities
    xiangqi_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties),
             (stream_tube_fixed_properties, stream_tube_allowed_properties),
             (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
            (xiangqi_fixed_properties, specialized_tube_allowed_properties)]})
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
            [contact_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][cs.CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

    # send presence with both D-Bus and stream tube caps
    presence = make_presence(contact, status='hello')
    c = presence.addElement((ns.CAPS, 'c'))
    c['node'] = client
    c['ver'] = compute_caps_hash([], [ns.TUBES + '/dbus#com.example.Xiangqi',
        ns.TUBES + '/stream#daap'], [])
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact, query_ns=ns.DISCO_INFO)
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ver']
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/dbus#com.example.Xiangqi'
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/stream#daap'
    stream.send(result)

    # daap + xiangqi capabilities
    daap_xiangqi_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
        (daap_fixed_properties, specialized_tube_allowed_properties),
        (xiangqi_fixed_properties, specialized_tube_allowed_properties)]})
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
            [contact_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][cs.CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

    # send presence with 4 tube caps
    presence = make_presence(contact, status='hello')
    c = presence.addElement((ns.CAPS, 'c'))
    c['node'] = client
    c['ver'] = compute_caps_hash([], [ns.TUBES + '/dbus#com.example.Xiangqi',
        ns.TUBES + '/stream#daap',  ns.TUBES + '/dbus#com.example.Go',
        ns.TUBES + '/stream#http' ], [])
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact, query_ns=ns.DISCO_INFO)
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ver']
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/dbus#com.example.Xiangqi'
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/dbus#com.example.Go'
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/stream#daap'
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/stream#http'
    stream.send(result)

    # http + daap + xiangqi + go capabilities
    all_tubes_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
        (daap_fixed_properties, specialized_tube_allowed_properties),
        (http_fixed_properties, specialized_tube_allowed_properties),
        (xiangqi_fixed_properties, specialized_tube_allowed_properties),
        (go_fixed_properties, specialized_tube_allowed_properties)]})
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
            [contact_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][cs.CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

    # send presence with both D-Bus and stream tube caps
    presence = make_presence(contact, status='hello')
    c = presence.addElement((ns.CAPS, 'c'))
    c['node'] = client
    c['ver'] = compute_caps_hash([], [ns.TUBES + '/dbus#com.example.Xiangqi',
        ns.TUBES + '/stream#daap'], [])
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble does not look up our capabilities because of the cache

    # daap + xiangqi capabilities
    daap_xiangqi_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
        (daap_fixed_properties, specialized_tube_allowed_properties),
        (xiangqi_fixed_properties, specialized_tube_allowed_properties)]})
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
            [contact_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][cs.CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

def test_tube_caps_to_contact(q, bus, conn, stream):
    basic_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
         (stream_tube_fixed_properties, stream_tube_allowed_properties),
         (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
         (ft_fixed_properties, ft_allowed_properties)]})
    daap_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
        (daap_fixed_properties, specialized_tube_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties)]})
    xiangqi_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
        (xiangqi_fixed_properties, specialized_tube_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties)]})
    daap_xiangqi_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
        (daap_fixed_properties, specialized_tube_allowed_properties),
        (xiangqi_fixed_properties, specialized_tube_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties)]})
    all_tubes_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
        (daap_fixed_properties, specialized_tube_allowed_properties),
        (http_fixed_properties, specialized_tube_allowed_properties),
        (xiangqi_fixed_properties, specialized_tube_allowed_properties),
        (go_fixed_properties, specialized_tube_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties)]})

    conn_caps_iface = dbus.Interface(conn, cs.CONN_IFACE_CONTACT_CAPS)
    conn_contacts_iface = dbus.Interface(conn, cs.CONN_IFACE_CONTACTS)

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == basic_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [1][cs.CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise nothing
    conn_caps_iface.SetSelfCapabilities([])

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert len(caps) == 1
    assert caps == basic_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [1][cs.CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    sync_stream(q, stream)

    # Advertise daap
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [daap_fixed_properties])

    # Expect Gabble to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, stream)
    assert caps_contain(event, ns.TUBES) == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#http') == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Xiangqi') \
            == False, caps_str
    assert signaled_caps == daap_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == daap_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [1][cs.CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise xiangqi
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [xiangqi_fixed_properties])

    # Expect Gabble to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, stream)
    assert caps_contain(event, ns.TUBES) == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#daap') == False, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#http') == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert signaled_caps == xiangqi_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [1][cs.CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise daap + xiangqi
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [daap_fixed_properties, xiangqi_fixed_properties])

    # Expect Gabble to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, stream)
    assert caps_contain(event, ns.TUBES) == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#http') == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert signaled_caps == daap_xiangqi_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == daap_xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [1][cs.CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise 4 tubes
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [daap_fixed_properties, http_fixed_properties,
         go_fixed_properties, xiangqi_fixed_properties])

    # Expect Gabble to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, stream)
    assert caps_contain(event, ns.TUBES) == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#http') == True, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Go') \
            == True, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert signaled_caps == all_tubes_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == all_tubes_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [1][cs.CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise daap + xiangqi
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [daap_fixed_properties, xiangqi_fixed_properties])

    # Expect Gabble to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, stream)
    assert caps_contain(event, ns.TUBES) == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#http') == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert signaled_caps == daap_xiangqi_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == daap_xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [1][cs.CONN_IFACE_CONTACT_CAPS + '/caps']
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

