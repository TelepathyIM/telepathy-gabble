"""
Tests receiving capabilities from bare JIDs.
"""

from twisted.words.xish import xpath

from servicetest import (
    assertEquals, assertContains, assertDoesNotContain, EventPattern,
    )
from gabbletest import make_presence, exec_test
from caps_helper import compute_caps_hash, make_caps_disco_reply
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    client = 'http://example.com/perverse-client'
    contact_bare_jid = 'edgecase@example.com'
    contact_with_resource = 'edgecase@example.com/hi'
    contact_handle = conn.RequestHandles(cs.HT_CONTACT, [contact_bare_jid])[0]

    # Gabble gets a presence stanza from a bare JID, which is a tad surprising.
    features = [
        ns.JINGLE_015,
        ns.JINGLE_015_AUDIO,
        ns.JINGLE_015_VIDEO,
        ns.GOOGLE_P2P,
        ]
    caps = {'node': client,
            'hash': 'sha-1',
            'ver': compute_caps_hash([], features, {}),
           }
    p = make_presence(contact_bare_jid, status='Hello', caps=caps)
    stream.send(p)

    # Gabble looks up the hash
    event = q.expect('stream-iq', to=contact_bare_jid,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assertEquals(client + '#' + caps['ver'], query_node.attributes['node'])

    # The bare jid replies
    result = make_caps_disco_reply(stream, event.stanza, features, {})
    stream.send(result)

    # Gabble lets us know their caps have changed. (Gabble used to ignore the
    # reply.)
    streamed_media_caps = (contact_handle, cs.CHANNEL_TYPE_STREAMED_MEDIA,
        0, 3, 0, cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_VIDEO)
    e = q.expect('dbus-signal', signal='CapabilitiesChanged')
    assertContains(streamed_media_caps, e.args[0])

    # Gabble gets another presence stanza from the bare JID, with different
    # caps.
    features.append(ns.TUBES)
    caps = {'node': client,
            'hash': 'sha-1',
            'ver': compute_caps_hash([], features, {}),
           }
    p = make_presence(contact_bare_jid, status='Get out the abacus', caps=caps)
    stream.send(p)

    # Gabble looks up the new hash
    disco2 = q.expect('stream-iq', to=contact_bare_jid,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', disco2.stanza)[0]
    assertEquals(client + '#' + caps['ver'], query_node.attributes['node'])

    # This time, before the bare JID replies, Gabble gets a presence from the
    # resourceful jid.
    features_ = features + [ns.CHAT_STATES]
    caps = {'node': client,
            'hash': 'sha-1',
            'ver': compute_caps_hash([], features_, {}),
           }
    p = make_presence(contact_with_resource, status='Count this', caps=caps)
    stream.send(p)

    # Gabble throws away presence from the bare JID when it gets presence from
    # a resource (and vice versa), so it should now say the contact is
    # incapable.  Gabble also looks up the resourceful JID's hash.
    cc, disco3 = q.expect_many(
        EventPattern('dbus-signal', signal='CapabilitiesChanged'),
        EventPattern('stream-iq', to=contact_with_resource,
            query_ns='http://jabber.org/protocol/disco#info'),
        )

    assertDoesNotContain(streamed_media_caps, cc.args[0])

    query_node = xpath.queryForNodes('/iq/query', disco3.stanza)[0]
    assertEquals(client + '#' + caps['ver'], query_node.attributes['node'])

    # The bare jid replies! Getting a disco reply from a bare JID when we've
    # got presence from resources used to crash Gabble, but now it just ignores
    # it.
    result = make_caps_disco_reply(stream, disco2.stanza, features, {})
    stream.send(result)

    # Now the resourceful JID replies:
    result = make_caps_disco_reply(stream, disco3.stanza, features_, {})
    stream.send(result)

    # Gabble should announce that the contact has acquired some caps.
    e = q.expect('dbus-signal', signal='CapabilitiesChanged')
    assertContains(streamed_media_caps, e.args[0])

if __name__ == '__main__':
    exec_test(test)
