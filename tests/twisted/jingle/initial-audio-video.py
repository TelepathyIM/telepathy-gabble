"""
Tests outgoing calls created with InitialAudio and/or InitialVideo.
"""

from servicetest import assertContains

from jingletest2 import JingleTest2, test_all_dialects

import constants as cs

def test(jp, q, bus, conn, stream):
    remote_jid = 'flames@cold.mountain/beyond'
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)
    jt.prepare()

    rccs = conn.Properties.Get(cs.CONN_IFACE_REQUESTS, 'RequestableChannelClasses')
    cclass = ({ cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
                cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              },
              [ cs.TARGET_HANDLE, cs.TARGET_ID,
                cs.INITIAL_AUDIO, cs.INITIAL_VIDEO,
              ]
             )
    assertContains(cclass, rccs)

if __name__ == '__main__':
    test_all_dialects(test)
