"""
Test that we cache our own capabilities, so that we don't disco other people
with the same caps hash or ext='' bundles.
"""
from twisted.words.xish import xpath
from twisted.words.protocols.jabber.client import IQ

from gabbletest import exec_test, make_presence, sync_stream
from servicetest import EventPattern, assertEquals, assertNotEquals
import ns
import constants as cs

from config import VOIP_ENABLED

if not VOIP_ENABLED:
    print "NOTE: built with --disable-voip"
    raise SystemExit(77)

def test(q, bus, conn, stream):
    self_presence = q.expect('stream-presence')

    c = xpath.queryForNodes('/presence/c', self_presence.stanza)[0]

    jid = 'lol@great.big/omg'

    # Gabble shouldn't send any disco requests to our contact during this test.
    q.forbid_events([
        EventPattern('stream-iq', to=jid, iq_type='get',
            query_ns=ns.DISCO_INFO),
    ])

    # Check that Gabble doesn't disco other clients with the same caps hash.
    p = make_presence(jid,
        caps={'node': c['node'],
              'hash': c['hash'],
              'ver':  c['ver'],
             })
    stream.send(p)
    sync_stream(q, stream)

    # Check that Gabble doesn't disco its own ext='' bundles (well, its own
    # bundles as advertised by Gabbles that don't do hashed caps)
    p = make_presence(jid,
        caps={'node': c['node'],
              'ver':  c['ver'],
              # omitting hash='' so Gabble doesn't ignore ext=''
              'ext':  'voice-v1 video-v1',
            })
    stream.send(p)
    sync_stream(q, stream)

    # Advertise some different capabilities, to change our own caps hash.
    add = [(cs.CHANNEL_TYPE_STREAMED_MEDIA, 2L**32-1),
           (cs.CHANNEL_TYPE_STREAM_TUBE, 2L**32-1),
           (cs.CHANNEL_TYPE_STREAM_TUBE, 2L**32-1)]
    remove = []
    caps = conn.Capabilities.AdvertiseCapabilities(add, remove)

    self_presence = q.expect('stream-presence')
    c_ = xpath.queryForNodes('/presence/c', self_presence.stanza)[0]
    assertNotEquals(c['ver'], c_['ver'])

    # But then someone asks us for our old caps
    iq = IQ(stream, 'get')
    iq['from'] = jid
    query = iq.addElement((ns.DISCO_INFO, 'query'))
    query['node'] = c['node'] + '#' + c['ver']
    stream.send(iq)

    # Gabble should still know what they are, and reply. This is actually quite
    # important: there's a bug in iChat where if you return an error to a disco
    # query, it just asks again, and again, and again...
    reply = q.expect('stream-iq', to=jid)
    assertEquals('result', reply.iq_type)

if __name__ == '__main__':
    exec_test(test)
