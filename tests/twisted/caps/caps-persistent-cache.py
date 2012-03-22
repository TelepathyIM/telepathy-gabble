
from twisted.words.xish import xpath

from servicetest import (
    assertEquals, assertContains, assertDoesNotContain, EventPattern,
    )
from gabbletest import make_presence, exec_test
from caps_helper import compute_caps_hash, send_disco_reply
import constants as cs
import ns

from config import VOIP_ENABLED

if not VOIP_ENABLED:
    print "NOTE: built with --disable-voip"
    raise SystemExit(77)

contact_bare_jid = 'macbeth@glamis'
contact_jid = 'macbeth@glamis/hall'
client = 'http://telepathy.freedesktop.org/zomg-ponies'
features = [
    ns.JINGLE_015,
    ns.JINGLE_015_AUDIO,
    ns.JINGLE_015_VIDEO,
    ns.GOOGLE_P2P,
    ns.TUBES + '/dbus#com.example.Xiangqi',
    ]
xiangqi_tube_cap = (
    {cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
      cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
      cs.DBUS_TUBE_SERVICE_NAME: u'com.example.Xiangqi'},
    [cs.TARGET_HANDLE, cs.TARGET_ID])

def send_presence(q, stream, contact_jid, identity):
    ver = compute_caps_hash([identity], features, {})
    stream.send(make_presence(contact_jid, status='Hello',
        caps={'node': client, 'hash': 'sha-1', 'ver': ver}))

def handle_disco(q, stream, contact_jid, identity):
    # Gabble tries to resolve a caps hash.
    ver = compute_caps_hash([identity], features, {})
    event = q.expect('stream-iq', to=contact_jid, query_ns=ns.DISCO_INFO)
    assertEquals(client + '#' + ver, event.query.attributes['node'])

    # The bare jid replies.
    send_disco_reply(stream, event.stanza, [identity], features)

def capabilities_changed(q, contact_handle):
    streamed_media_caps = (contact_handle, cs.CHANNEL_TYPE_STREAMED_MEDIA,
        0, 3, 0, cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_VIDEO)
    e = q.expect('dbus-signal', signal='CapabilitiesChanged')
    assertContains(streamed_media_caps, e.args[0])
    e = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assertContains(contact_handle, e.args[0])
    assertContains(xiangqi_tube_cap, e.args[0][contact_handle])

def test1(q, bus, conn, stream):
    contact_handle = conn.RequestHandles(cs.HT_CONTACT, [contact_bare_jid])[0]
    send_presence(q, stream, contact_jid, 'client/pc//thane')
    handle_disco(q, stream, contact_jid, 'client/pc//thane')
    capabilities_changed(q, contact_handle)

def test2(q, bus, conn, stream):
    # The second time around, the capabilities are retrieved from the cache,
    # so no disco request is sent.
    contact_handle = conn.RequestHandles(cs.HT_CONTACT, [contact_bare_jid])[0]
    send_presence(q, stream, contact_jid, 'client/pc//thane')
    capabilities_changed(q, contact_handle)

    # Overflow the cache. GC is considered every 50 inserts, and then only
    # performed if the cache has more entries than a threshold which is set to
    # 50 in the test suite, reducing the cache to 0.95 * that threshold, which
    # is 47 in the test suite.
    #
    # We want to ensure that Macbeth is removed from the cache. In the worst
    # case, GC is considered and performed when we insert the 46th witch, which
    # will leave Macbeth in the cache. Inserting a further 50 witches will
    # ensure that Macbeth is flushed even in this worst case. Let's round up to
    # 100 witches.

    for i in range(100):
        overflow_contact_jid = 'witch%d@forest/cauldron' % i
        overflow_identity = 'client/pc//prophecy%d' % i
        send_presence(q, stream, overflow_contact_jid, overflow_identity)
        handle_disco(q, stream, overflow_contact_jid, overflow_identity)

if __name__ == '__main__':
    # We run test1. The capabilities for macbeth@glamis's client
    # need to be fetched via disco and are then stored in the cache.
    exec_test(test1)
    # We run test2 again. The capabilities are retrieved from the cache, so no
    # disco request is sent. Then, a bunch of other clients turn up and force
    # the entry for Macbeth's client out of the cache.
    exec_test(test2)
    # We run test1 again. The caps are no longer in the cache, so a disco
    # request is sent again.
    exec_test(test1)

