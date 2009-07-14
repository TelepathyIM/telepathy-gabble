"""
Test making outgoing calls using EnsureChannel, and retrieving existing calls
using EnsureChannel.
"""

from gabbletest import exec_test
from servicetest import (
    wrap_channel,
    call_async, EventPattern,
    assertEquals, assertLength,
    )
import constants as cs
from jingletest2 import JingleProtocol031, JingleTest2


def test(q, bus, conn, stream):
    jt = JingleTest2(JingleProtocol031(), conn, q, stream, 'test@localhost',
        'foo@bar.com/Foo')
    jt.prepare()

    self_handle = conn.GetSelfHandle()
    handle = conn.RequestHandles(cs.HT_CONTACT, [jt.peer])[0]

    # Ensure a channel that doesn't exist yet.
    call_async(q, conn.Requests, 'EnsureChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: handle,
              })

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='EnsureChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    yours, path, props = ret.value

    # this channel was created in response to our EnsureChannel call, so it
    # should be ours.
    assert yours, ret.value

    sig_path, sig_ct, sig_ht, sig_h, sig_sh = old_sig.args

    assertEquals(sig_path, path)
    assertEquals(cs.CHANNEL_TYPE_STREAMED_MEDIA, sig_ct)
    assertEquals(cs.HT_CONTACT, sig_ht)
    assertEquals(handle, sig_h)
    assert sig_sh # suppress handler

    assertLength(1, new_sig.args)
    assertLength(1, new_sig.args[0])        # one channel
    assertLength(2, new_sig.args[0][0])     # two struct members
    assertEquals(path, new_sig.args[0][0][0])
    emitted_props = new_sig.args[0][0][1]

    assertEquals(cs.CHANNEL_TYPE_STREAMED_MEDIA, emitted_props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, emitted_props[cs.TARGET_HANDLE_TYPE])
    assertEquals(handle, emitted_props[cs.TARGET_HANDLE])
    assertEquals('foo@bar.com', emitted_props[cs.TARGET_ID])
    assert emitted_props[cs.REQUESTED]
    assertEquals(self_handle, emitted_props[cs.INITIATOR_HANDLE])
    assertEquals('test@localhost', emitted_props[cs.INITIATOR_ID])

    # Now ensure a media channel with the same contact, and check it's the
    # same.
    call_async(q, conn.Requests, 'EnsureChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: handle,
              })

    event = q.expect('dbus-return', method='EnsureChannel')
    yours2, path2, props2 = event.value

    # We should have got back the same channel we created a page or so ago.
    assertEquals(path2, path)
    # It's not been created for this call, so Yours should be False.
    assert not yours2

    # Time passes ... afterwards we close the chan

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia')
    chan.Close()


    # Now, create an anonymous channel with RequestChannel, add the other
    # person to it with RequestStreams, then Ensure a media channel with that
    # person.  We should get the anonymous channel back.
    call_async(
        q, conn, 'RequestChannel', cs.CHANNEL_TYPE_STREAMED_MEDIA, 0, 0, True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    path = ret.value[0]
    assertEquals(
        [path, cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.HT_NONE, 0, True],
        old_sig.args)

    assertLength(1, new_sig.args)
    assertLength(1, new_sig.args[0])        # one channel
    assertLength(2, new_sig.args[0][0])     # two struct members
    assertEquals(path, new_sig.args[0][0][0])
    emitted_props = new_sig.args[0][0][1]

    assertEquals(cs.CHANNEL_TYPE_STREAMED_MEDIA, emitted_props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_NONE, emitted_props[cs.TARGET_HANDLE_TYPE])
    assertEquals(0, emitted_props[cs.TARGET_HANDLE])
    assertEquals('', emitted_props[cs.TARGET_ID])
    assert emitted_props[cs.REQUESTED]
    assertEquals(self_handle, emitted_props[cs.INITIATOR_HANDLE])
    assertEquals('test@localhost', emitted_props[cs.INITIATOR_ID])

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia')

    # Request streams with the other person.  This should make them the
    # channel's "peer" property.
    chan.StreamedMedia.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_AUDIO])

    # Now, Ensuring a media channel with handle should yield the channel just
    # created.

    call_async(q, conn.Requests, 'EnsureChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: handle,
              })

    event = q.expect('dbus-return', method='EnsureChannel')
    yours, path2, _ = event.value

    # we should have got back the anonymous channel we got with requestchannel
    # and called RequestStreams(handle) on.
    assertEquals(path2, path)
    # It's not been created for this call, so Yours should be False.
    assert not yours

    chan.Close()

if __name__ == '__main__':
    exec_test(test)
