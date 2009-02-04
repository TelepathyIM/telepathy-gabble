
"""
Test that Gabble properly cleans up delayed RequestStream contexts
and returns an error when Disconnect is called and there are
incomplete requests.
"""

from gabbletest import exec_test, make_result_iq, sync_stream
from servicetest import make_channel_proxy, unwrap, tp_path_prefix, \
        call_async, EventPattern, sync_dbus
from twisted.words.xish import domish
import jingletest
import gabbletest
import dbus
import time

import constants as cs

def test(q, bus, conn, stream):
    jt = jingletest.JingleTest(stream, 'test@localhost', 'foo@bar.com/Foo')

    # If we need to override remote caps, feats, codecs or caps,
    # this is a good time to do it

    # Connecting
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])

    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{1L: (0L, {u'available': {}})}])
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()

    # We intentionally DON'T set remote presence yet. Since Gabble is still
    # unsure whether to treat contact as offline for this purpose, it
    # will tentatively allow channel creation and contact handle addition

    handle = conn.RequestHandles(cs.HT_CONTACT, [jt.remote_jid])[0]

    path = conn.RequestChannel(cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.HT_CONTACT,
        handle, True)
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')

    # So it turns out that the calls to RequestStreams and Disconnect could be
    # reordered while the first waits on the result of introspecting the
    # channel's object which is kicked off by making a proxy object for it,
    # whereas the connection proxy is long ago introspected. Isn't dbus-python
    # great? Syncing here forces that introspection to finish so we can rely on
    # the ordering of RequestStreams and Disconnect. Yay.
    sync_dbus(bus, q, conn)

    # Now we request streams before either <presence> or caps have arrived
    call_async(q, media_iface, 'RequestStreams', handle,
        [cs.MEDIA_STREAM_TYPE_AUDIO])

    conn.Disconnect()

    # RequestStreams should now return NotAvailable
    event = q.expect('dbus-error', method='RequestStreams')
    assert event.error.get_dbus_name() == cs.NOT_AVAILABLE, event.error

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True


if __name__ == '__main__':
    exec_test(test, timeout=10)

