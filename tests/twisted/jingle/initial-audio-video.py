"""
Tests outgoing calls created with InitialAudio and/or InitialVideo.
"""

from servicetest import (
    assertContains, assertEquals, wrap_channel, EventPattern, call_async,
    )

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

    check_neither(jp, q, conn, bus, stream, remote_handle)
    check_iav(jp, q, conn, bus, stream, remote_handle, True, False)
    check_iav(jp, q, conn, bus, stream, remote_handle, False, True)
    check_iav(jp, q, conn, bus, stream, remote_handle, True, True)

def check_neither(jp, q, conn, bus, stream, remote_handle):
    """
    Make a channel without specifying InitialAudio or InitialVideo; check
    that it's announced with both False, and that they're both present and
    false in GetAll().
    """

    path, props = conn.Requests.CreateChannel({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_HANDLE: remote_handle})

    assertContains((cs.INITIAL_AUDIO, False), props.items())
    assertContains((cs.INITIAL_VIDEO, False), props.items())

    chan = wrap_channel(bus.get_object(conn.bus_name, path),
        cs.CHANNEL_TYPE_STREAMED_MEDIA)
    props = chan.Properties.GetAll(cs.CHANNEL_TYPE_STREAMED_MEDIA + '.FUTURE')
    assertContains(('InitialAudio', False), props.items())
    assertContains(('InitialVideo', False), props.items())

def check_iav(jp, q, conn, bus, stream, remote_handle, initial_audio,
              initial_video):
    """
    Make a channel and check that its InitialAudio and InitialVideo properties
    come out correctly.
    """

    call_async(q, conn.Requests, 'CreateChannel', {
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_HANDLE: remote_handle,
        cs.INITIAL_AUDIO: initial_audio,
        cs.INITIAL_VIDEO: initial_video,
        })
    if initial_video and jp.is_gtalk():
        # You can't do video on ye olde GTalk.
        event = q.expect('dbus-error', method='CreateChannel')
        assertEquals(cs.NOT_CAPABLE, event.error.get_dbus_name())
    else:
        path, props = q.expect('dbus-return', method='CreateChannel').value

        assertContains((cs.INITIAL_AUDIO, initial_audio), props.items())
        assertContains((cs.INITIAL_VIDEO, initial_video), props.items())

        chan = wrap_channel(bus.get_object(conn.bus_name, path),
            cs.CHANNEL_TYPE_STREAMED_MEDIA)
        props = chan.Properties.GetAll(cs.CHANNEL_TYPE_STREAMED_MEDIA + '.FUTURE')
        assertContains(('InitialAudio', initial_audio), props.items())
        assertContains(('InitialVideo', initial_video), props.items())

        chan.Close()

if __name__ == '__main__':
    test_all_dialects(test)
