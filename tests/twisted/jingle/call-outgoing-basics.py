"""
Test basic outgoing call handling, using CreateChannel
"""

import dbus
from twisted.words.xish import xpath

from gabbletest import exec_test
from servicetest import (
    make_channel_proxy, wrap_channel,
    EventPattern, call_async,
    assertEquals, assertContains, assertLength,
    )
import constants as cs
from jingletest2 import JingleTest2, test_all_dialects

def run_test(jp, q, bus, conn, stream):
    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]

    # Ensure a channel that doesn't exist yet.
    call_async(q, conn.Requests, 'CreateChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: remote_handle,
              cs.CALL_INITIAL_AUDIO: True,
            })

    ret, signal = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
    )

    assertLength(1, signal.args)
    assertLength(1, signal.args[0])       # one channel
    assertLength(2, signal.args[0][0])    # two struct members
    emitted_props = signal.args[0][0][1]

    assertEquals(
        cs.CHANNEL_TYPE_CALL, emitted_props[cs.CHANNEL_TYPE])

    assertEquals(remote_handle, emitted_props[cs.TARGET_HANDLE])
    assertEquals(cs.HT_CONTACT, emitted_props[cs.TARGET_HANDLE_TYPE])
    assertEquals('foo@bar.com', emitted_props[cs.TARGET_ID])

    assertEquals(True, emitted_props[cs.REQUESTED])
    assertEquals(self_handle, emitted_props[cs.INITIATOR_HANDLE])
    assertEquals('test@localhost', emitted_props[cs.INITIATOR_ID])

    assertEquals(True, emitted_props[cs.CALL_INITIAL_AUDIO])
    assertEquals(False, emitted_props[cs.CALL_INITIAL_VIDEO])

    chan = bus.get_object (conn.bus_name, ret.value[0])

    properties = chan.GetAll(cs.CHANNEL_TYPE_CALL,
        dbus_interface=dbus.PROPERTIES_IFACE)

    # Only an audio content
    assertEquals (1, len(properties["Contents"]))

    content = bus.get_object (conn.bus_name, properties["Contents"][0])

    content_properties = content.GetAll (cs.CALL_CONTENT,
        dbus_interface=dbus.PROPERTIES_IFACE)

    # Has one stream
    assertEquals (1, len(content_properties["Streams"]))

    stream = bus.get_object (conn.bus_name, content_properties["Streams"][0])

    # Setup codecs
    codecs = jt2.get_call_audio_codecs_dbus()
    content.SetCodecs(codecs, dbus_interface=cs.CALL_CONTENT_IFACE_MEDIA)

    current_codecs = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "ContactCodecMap", dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (codecs,  current_codecs[self_handle])

    # Add candidates
    candidates = jt2.get_call_remote_transports_dbus ()
    stream.AddCandidates (candidates,
        dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    local_candidates = stream.Get(cs.CALL_STREAM_IFACE_MEDIA,
                "LocalCandidates", dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (candidates,  local_candidates)

if __name__ == '__main__':
    test_all_dialects(run_test)
