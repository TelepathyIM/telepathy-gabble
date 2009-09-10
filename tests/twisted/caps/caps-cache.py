
"""
Test that requesting a caps set 1 time is enough with hash and that we need 5
confirmation without hash.
"""

from twisted.words.xish import xpath

from servicetest import EventPattern, assertEquals, assertContains
from gabbletest import exec_test, make_presence
import constants as cs
import ns
from caps_helper import (
    compute_caps_hash, make_caps_disco_reply, fake_client_dataforms,
    )

client = 'http://telepathy.freedesktop.org/fake-client'
features = [
    'http://jabber.org/protocol/jingle',
    'http://jabber.org/protocol/jingle/description/audio',
    'http://www.google.com/transport/p2p',
    ]

def presence_and_disco(q, conn, stream, contact, disco,
                       caps, dataforms={}):
    h = send_presence(q, conn, stream, contact, caps)

    if disco:
        stanza = expect_disco(q, contact, caps)
        send_disco_reply(stream, stanza, dataforms)

    expect_caps(q, conn, h)

def send_presence(q, conn, stream, contact, caps):
    h = conn.RequestHandles(cs.HT_CONTACT, [contact])[0]

    stream.send(make_presence(contact, status='hello'))

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
            args=[{h:
               (0L, {u'available': {'message': 'hello'}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
            args=[{h:
               (2, u'available', 'hello')}]))

    # no special capabilities
    assertEquals([(h, cs.CHANNEL_TYPE_TEXT, 3, 0)],
        conn.Capabilities.GetCapabilities([h]))

    # send updated presence with Jingle caps info
    stream.send(make_presence(contact, status='hello', caps=caps))

    return h

def expect_disco(q, contact, caps):
    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact, query_ns=ns.DISCO_INFO)
    assertEquals(client + '#' + caps['ver'], event.query['node'])

    return event.stanza

def send_disco_reply(stream, stanza, dataforms):
    # send good reply
    result = make_caps_disco_reply(stream, stanza, features, dataforms)
    stream.send(result)

def expect_caps(q, conn, h):
    # we can now do audio calls
    event = q.expect('dbus-signal', signal='CapabilitiesChanged')
    assertContains((h, cs.CHANNEL_TYPE_STREAMED_MEDIA, 3, cs.MEDIA_CAP_AUDIO),
        conn.Capabilities.GetCapabilities([h]))

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    caps = {
        'node': client,
        'ver':  '0.1',
        }

    presence_and_disco(q, conn, stream, 'bob1@foo.com/Foo', True, caps)
    presence_and_disco(q, conn, stream, 'bob2@foo.com/Foo', True, caps)
    presence_and_disco(q, conn, stream, 'bob3@foo.com/Foo', True, caps)
    presence_and_disco(q, conn, stream, 'bob4@foo.com/Foo', True, caps)
    presence_and_disco(q, conn, stream, 'bob5@foo.com/Foo', True, caps)
    # we have 5 different contacts that confirm
    presence_and_disco(q, conn, stream, 'bob6@foo.com/Foo', False, caps)
    presence_and_disco(q, conn, stream, 'bob7@foo.com/Foo', False, caps)

    caps = {
        'node': client,
        'ver':  compute_caps_hash([], features, fake_client_dataforms),
        'hash': 'sha-1',
        }

    presence_and_disco(q, conn, stream, 'bilbo1@foo.com/Foo', True, caps,
        fake_client_dataforms)
    # 1 contact is enough with hash
    presence_and_disco(q, conn, stream, 'bilbo2@foo.com/Foo', False, caps,
        fake_client_dataforms)
    presence_and_disco(q, conn, stream, 'bilbo3@foo.com/Foo', False, caps,
        fake_client_dataforms)

if __name__ == '__main__':
    exec_test(test)
