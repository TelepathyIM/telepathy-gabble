"""
Tests receiving capabilities from bare JIDs.
"""

from twisted.words.xish import xpath

from servicetest import (
    assertEquals, assertContains, assertDoesNotContain, EventPattern,
    )
from gabbletest import make_presence, exec_test
from caps_helper import (compute_caps_hash, send_disco_reply,
        assert_rccs_callable, assert_rccs_not_callable)
import constants as cs
import ns

from config import VOIP_ENABLED

if not VOIP_ENABLED:
    print "NOTE: built with --disable-voip"
    raise SystemExit(77)

def test(q, bus, conn, stream):
    client = 'http://example.com/perverse-client'
    contact_bare_jid = 'edgecase@example.com'
    contact_with_resource = 'edgecase@example.com/hi'
    contact_handle = conn.get_contact_handle_sync(contact_bare_jid)

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
    send_disco_reply(stream, event.stanza, [], features)

    # Gabble lets us know their caps have changed. (Gabble used to ignore the
    # reply.)
    cc, = q.expect_many(
        EventPattern('dbus-signal', signal='ContactCapabilitiesChanged'),
        )
    assert_rccs_callable(cc.args[0][contact_handle])

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
        EventPattern('dbus-signal', signal='ContactCapabilitiesChanged'),
        EventPattern('stream-iq', to=contact_with_resource,
            query_ns='http://jabber.org/protocol/disco#info'),
        )

    assert_rccs_not_callable(cc.args[0][contact_handle])

    query_node = xpath.queryForNodes('/iq/query', disco3.stanza)[0]
    assertEquals(client + '#' + caps['ver'], query_node.attributes['node'])

    # The bare jid replies! Getting a disco reply from a bare JID when we've
    # got presence from resources used to crash Gabble, but now it just ignores
    # it.
    send_disco_reply(stream, disco2.stanza, [], features)

    # Now the resourceful JID replies:
    send_disco_reply(stream, disco3.stanza, [], features_)

    # Gabble should announce that the contact has acquired some caps.
    cc, = q.expect_many(
        EventPattern('dbus-signal', signal='ContactCapabilitiesChanged'),
        )
    assert_rccs_callable(cc.args[0][contact_handle])

if __name__ == '__main__':
    exec_test(test)
