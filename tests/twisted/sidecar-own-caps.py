"""
Test Gabble's implementation of sidecars own caps, using the test plugin.
"""

from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import xpath

from servicetest import call_async, EventPattern, assertEquals, assertContains
from gabbletest import exec_test, acknowledge_iq
from caps_helper import compute_caps_hash
import constants as cs
import ns
from config import PLUGINS_ENABLED

TEST_PLUGIN_IFACE = "org.freedesktop.Telepathy.Gabble.Plugin.Test"

if not PLUGINS_ENABLED:
    print "NOTE: built without --enable-plugins, skipping"
    raise SystemExit(77) # which makes the test show up as skipped

def test(q, bus, conn, stream):
    # This sidecar sends a stanza, and waits for a reply, before being
    # created.
    pattern = EventPattern('stream-iq', to='sidecar.example.com',
        query_ns='http://example.com/sidecar')
    call_async(q, conn.Future, 'EnsureSidecar', TEST_PLUGIN_IFACE + ".IQ")
    e = q.expect_many(pattern)[0]

    # The server said yes, so we should get a sidecar back!
    acknowledge_iq(stream, e.stanza)
    q.expect('dbus-return', method='EnsureSidecar')

    identities = ["test/app-list//Test"]
    features = ["com.example.test1", "com.example.test2"]
    ver = compute_caps_hash(identities, features, {})

    iq = IQ(stream, "get")
    id = iq['id']
    query = iq.addElement((ns.DISCO_INFO, 'query'))
    query['node'] = ns.GABBLE_CAPS + '#' + ver
    stream.send(iq)
    e = q.expect('stream-iq', query_ns='http://jabber.org/protocol/disco#info')

    returned_features = [feature['var']
        for feature in xpath.queryForNodes('/iq/query/feature', e.stanza)]
    assertEquals(features, returned_features)

    returned_identities = [identity['category'] + "/" + identity['type']+"//" + identity['name']
        for identity in xpath.queryForNodes('/iq/query/identity', e.stanza)]
    assertEquals(identities, returned_identities)

    new_ver = compute_caps_hash(returned_identities, returned_features, {})
    assertEquals(new_ver, ver)

if __name__ == '__main__':
    exec_test(test)
