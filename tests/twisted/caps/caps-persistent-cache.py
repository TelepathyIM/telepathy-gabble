
from twisted.words.xish import xpath

from servicetest import (
    assertEquals, assertContains, assertDoesNotContain, EventPattern,
    )
from gabbletest import make_presence, exec_test
from caps_helper import compute_caps_hash, send_disco_reply
import constants as cs
import ns

contact_bare_jid = 'macbeth@glamis'
contact_jid = 'macbeth@glamis/hall'
client = 'http://telepathy.freedesktop.org/zomg-ponies'
features = [
    ns.JINGLE_015,
    ns.JINGLE_015_AUDIO,
    ns.JINGLE_015_VIDEO,
    ns.GOOGLE_P2P,
    ]

def connect(q, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

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

def test1(q, bus, conn, stream):
    connect(q, conn)
    contact_handle = conn.RequestHandles(cs.HT_CONTACT, [contact_bare_jid])[0]
    send_presence(q, stream, contact_jid, 'client/pc//thane')
    handle_disco(q, stream, contact_jid, 'client/pc//thane')
    capabilities_changed(q, contact_handle)

def test2(q, bus, conn, stream):
    # The second time around, the capabilities are retrieved from the cache,
    # so no disco request is sent.
    connect(q, conn)
    contact_handle = conn.RequestHandles(cs.HT_CONTACT, [contact_bare_jid])[0]
    send_presence(q, stream, contact_jid, 'client/pc//thane')
    capabilities_changed(q, contact_handle)

    # Overflow the cache. 51 is the cache size (during test runs) plus one.

    for i in range(51):
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

