"""
Test that we cache our own capabilities, so that we don't disco other people
with the same caps hash or ext='' bundles.
"""
from twisted.words.xish import xpath

from gabbletest import exec_test, make_presence, sync_stream
from servicetest import EventPattern
import ns

def test(q, bus, conn, stream):
    conn.Connect()
    self_presence = q.expect('stream-presence')

    c = xpath.queryForNodes('/presence/c', self_presence.stanza)[0]

    jid = 'lol@great.big/omg'
    p = make_presence(jid,
        caps={'node': c['node'],
              'hash': c['hash'],
              'ver':  c['ver'],
             })
    stream.send(p)

    q.forbid_events([
        EventPattern('stream-iq', to=jid, query_ns=ns.DISCO_INFO),
    ])
    sync_stream(q, stream)

    p = make_presence(jid,
        caps={'node': c['node'],
              'ver':  c['ver'],
              # omitting hash='' so Gabble doesn't ignore ext=''
              'ext':  'voice-v1 video-v1',
            })
    stream.send(p)
    sync_stream(q, stream)

if __name__ == '__main__':
    exec_test(test)
