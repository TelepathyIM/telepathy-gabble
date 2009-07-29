"""
Test use-case when client requests going online and immediately
attempts to call a contact. Gabble should delay the RequestStreams
call until caps have arrived.
"""

from gabbletest import exec_test
from servicetest import make_channel_proxy, call_async, sync_dbus
import jingletest

import dbus

import constants as cs
import ns

def test(q, bus, conn, stream):
    jt = jingletest.JingleTest(stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2 = jingletest.JingleTest(stream, 'test@localhost', 'foo2@bar.com/Foo')
    # Make gabble think this is a different client
    jt2.remote_caps['node'] = 'http://example.com/fake-client1'

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{1L: (0L, {u'available': {}})}])
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    run_test(q, bus, conn, stream, jt, True)
    run_test(q, bus, conn, stream, jt2, False)

def run_test(q, bus, conn, stream, jt, request_before_presence):
    """
    Requests streams on a media channel to jt.remote_jid, either before their
    presence is received (if request_before_presence is True) or after their
    presence is received but before we've got a disco response for their
    capabilities (otherwise).
    """

    # We intentionally DON'T set remote presence yet. Since Gabble is still
    # unsure whether to treat contact as offline for this purpose, it
    # will tentatively allow channel creation and contact handle addition

    request = dbus.Dictionary({ cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
                                cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
                                cs.TARGET_ID: jt.remote_jid
                              }, signature='sv')
    path, props = conn.CreateChannel(request, dbus_interface=cs.CONN_IFACE_REQUESTS)
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')
    handle = props[cs.TARGET_HANDLE]

    sync_dbus(bus, q, conn)

    def call_request_streams():
        call_async(q, media_iface, 'RequestStreams', handle,
            [cs.MEDIA_STREAM_TYPE_AUDIO])

    def send_presence():
        jt.send_remote_presence()
        return q.expect('stream-iq', query_ns=ns.DISCO_INFO, to=jt.remote_jid)

    if request_before_presence:
        # Request streams before either <presence> or caps have arrived. Gabble
        # should wait for both to arrive before returning from RequestStreams.
        call_request_streams()

        # Ensure Gabble's received the method call.
        sync_dbus(bus, q, conn)

        # Now send the presence.
        info_event = send_presence()
    else:
        info_event = send_presence()

        # Now call RequestStreams; it should wait for the disco reply.
        call_request_streams()

    jt.send_remote_disco_reply(info_event.stanza)

    # RequestStreams should now happily complete
    q.expect('dbus-return', method='RequestStreams')

if __name__ == '__main__':
    exec_test(test, timeout=10)

