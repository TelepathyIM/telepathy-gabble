"""
Tests outgoing calls created with InitialAudio and/or InitialVideo.
"""

from servicetest import assertContains, wrap_channel, EventPattern
from gabbletest import sync_stream

from jingletest2 import JingleTest2, test_all_dialects

import constants as cs

def test(jp, q, bus, conn, stream):
    remote_jid = 'flames@cold.mountain/beyond'
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)
    jt.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, [remote_jid])[0]

    rccs = conn.Properties.Get(cs.CONN_IFACE_REQUESTS, 'RequestableChannelClasses')
    cclass = ({ cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
                cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              },
              [ cs.TARGET_HANDLE, cs.TARGET_ID,
                cs.INITIAL_AUDIO, cs.INITIAL_VIDEO,
              ]
             )
    assertContains(cclass, rccs)

    # Make a channel without specifying InitialAudio or InitialVideo; check
    # that it's announced with both False, and that they're both present and
    # false in GetAll(). Also, make sure that nothing's sent to the peer.
    si = EventPattern('stream-iq', predicate=lambda e:
        jp.match_jingle_action(e.query, 'session-initiate'))
    q.forbid_events([si])

    path, props = conn.Requests.CreateChannel({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_HANDLE: remote_handle})
    assertContains((cs.INITIAL_AUDIO, False), props.items())
    assertContains((cs.INITIAL_VIDEO, False), props.items())

    chan = wrap_channel(bus.get_object(conn.bus_name, path), cs.CHANNEL_TYPE_STREAMED_MEDIA)
    props = chan.Properties.GetAll(cs.CHANNEL_TYPE_STREAMED_MEDIA + '.FUTURE')
    assertContains(('InitialAudio', False), props.items())
    assertContains(('InitialVideo', False), props.items())

    # Get rid of this channel.
    chan.Close()

    sync_stream(q, stream)
    q.unforbid_events([si])

if __name__ == '__main__':
    test_all_dialects(test)
