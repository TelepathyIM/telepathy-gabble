"""
Test that Gabble properly cleans up delayed RequestStream contexts
and returns an error when Disconnect is called and there are
incomplete requests.
"""

from gabbletest import exec_test, disconnect_conn
from servicetest import make_channel_proxy, call_async, sync_dbus, EventPattern
import jingletest

import constants as cs

def test(q, bus, conn, stream):
    jt = jingletest.JingleTest(stream, 'test@localhost', 'foo@bar.com/Foo')

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{1L: (0L, {u'available': {}})}])
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

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

    event = disconnect_conn(q, conn, stream,
        [EventPattern('dbus-error', method='RequestStreams')])[0]

    # RequestStreams should now return NotAvailable
    assert event.error.get_dbus_name() == cs.NOT_AVAILABLE, event.error

if __name__ == '__main__':
    exec_test(test, timeout=10)

