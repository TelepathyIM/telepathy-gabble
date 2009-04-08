
"""
Test that requesting a caps set 1 time is enough with hash and that we need 5
confirmation without hash.
"""

from twisted.words.xish import xpath

from servicetest import EventPattern
from gabbletest import exec_test, make_result_iq, make_presence
import constants as cs

def presence_add_caps(presence, ver, client, hash=None):
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = ver
    if hash is not None:
        c['hash'] = hash
    return presence

def _test_without_hash(q, bus, conn, stream, contact, contact_handle, client, disco):

    presence = make_presence(contact, status='hello')
    stream.send(presence)

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
            args=[{contact_handle:
               (0L, {u'available': {'message': 'hello'}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
            args=[{contact_handle:
               (2, u'available', 'hello')}]))


    # no special capabilities
    basic_caps = [(contact_handle, cs.CHANNEL_TYPE_TEXT, 3, 0)]
    assert conn.Capabilities.GetCapabilities([contact_handle]) == basic_caps

    # send updated presence with Jingle caps info
    presence = make_presence(contact, status='hello')
    presence = presence_add_caps(presence, '0.1', client)
    stream.send(presence)

    if disco:
        # Gabble looks up our capabilities
        event = q.expect('stream-iq', to=contact,
            query_ns='http://jabber.org/protocol/disco#info')
        query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
        assert query_node.attributes['node'] == \
            client + '#' + '0.1'

        # send good reply
        result = make_result_iq(stream, event.stanza)
        query = result.firstChildElement()
        feature = query.addElement('feature')
        feature['var'] = 'http://jabber.org/protocol/jingle'
        feature = query.addElement('feature')
        feature['var'] = 'http://jabber.org/protocol/jingle/description/audio'
        feature = query.addElement('feature')
        feature['var'] = 'http://www.google.com/transport/p2p'
        stream.send(result)

    # we can now do audio calls
    event = q.expect('dbus-signal', signal='CapabilitiesChanged')

def _test_with_hash(q, bus, conn, stream, contact, contact_handle, client, disco):

    presence = make_presence(contact, status='hello')
    stream.send(presence)

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
            args=[{contact_handle:
               (0L, {u'available': {'message': 'hello'}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
            args=[{contact_handle:
               (2, u'available', 'hello')}]))

    # no special capabilities
    basic_caps = [(contact_handle, cs.CHANNEL_TYPE_TEXT, 3, 0)]
    assert conn.Capabilities.GetCapabilities([contact_handle]) == basic_caps

    # send updated presence with Jingle caps info
    presence = make_presence(contact, status='hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = 'CzO+nkbflbxu1pgzOQSIi8gOyDc=' # good hash
    c['hash'] = 'sha-1'
    stream.send(presence)

    if disco:
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
        query.addRawXml("""
<x type='result' xmlns='jabber:x:data'>
<field var='FORM_TYPE' type='hidden'>
<value>urn:xmpp:dataforms:softwareinfo</value>
</field>
<field var='software'>
<value>A Fake Client with Twisted</value>
</field>
<field var='software_version'>
<value>5.11.2-svn-20080512</value>
</field>
<field var='os'>
<value>Debian GNU/Linux unstable (sid) unstable sid</value>
</field>
<field var='os_version'>
<value>2.6.24-1-amd64</value>
</field>
</x>
        """)
        stream.send(result)

    # we can now do audio calls
    event = q.expect('dbus-signal', signal='CapabilitiesChanged')
    assert conn.Capabilities.GetCapabilities([contact_handle]) != basic_caps

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    client = 'http://telepathy.freedesktop.org/fake-client'

    _test_without_hash(q, bus, conn, stream, 'bob1@foo.com/Foo', 2L, client,
        True)
    _test_without_hash(q, bus, conn, stream, 'bob2@foo.com/Foo', 3L, client,
        True)
    _test_without_hash(q, bus, conn, stream, 'bob3@foo.com/Foo', 4L, client,
        True)
    _test_without_hash(q, bus, conn, stream, 'bob4@foo.com/Foo', 5L, client,
        True)
    _test_without_hash(q, bus, conn, stream, 'bob5@foo.com/Foo', 6L, client,
        True)
    # we have 5 different contacts that confirm
    _test_without_hash(q, bus, conn, stream, 'bob6@foo.com/Foo', 7L, client,
        False)
    _test_without_hash(q, bus, conn, stream, 'bob7@foo.com/Foo', 8L, client,
        False)

    _test_with_hash(q, bus, conn, stream, 'bilbo1@foo.com/Foo', 9L, client,
        True)
    # 1 contact is enough with hash
    _test_with_hash(q, bus, conn, stream, 'bilbo2@foo.com/Foo', 10L, client,
        False)
    _test_with_hash(q, bus, conn, stream, 'bilbo3@foo.com/Foo', 11L, client,
        False)

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])


if __name__ == '__main__':
    exec_test(test)

