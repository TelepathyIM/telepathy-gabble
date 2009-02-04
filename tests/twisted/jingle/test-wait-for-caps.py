
"""
Test use-case when client requests going online and immediately
attempts to call a contact. Gabble should delay the RequestStreams
call until caps have arrived.
"""

from gabbletest import exec_test, make_result_iq, sync_stream
from servicetest import make_channel_proxy, unwrap, tp_path_prefix, \
        call_async, EventPattern
from twisted.words.xish import domish
import jingletest
import gabbletest
import dbus
import time

import constants as cs
import ns

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

    # Now we request streams before either <presence> or caps have arrived
    call_async(q, media_iface, 'RequestStreams', handle, [0]) # req audio stream

    # Variant of the "make sure disco is processed" test hack, but this time
    # we want to make sure RequestStreams is processed (and suspended) before
    # presence arrives, to be able to test it properl.y
    el = domish.Element(('jabber.client', 'presence'))
    el['from'] = 'bob@example.com/Bar'
    stream.send(el.toXml())
    q.expect('dbus-signal', signal='PresenceUpdate')
    # OK, now we can continue. End of hack

    # Only now we send the presence and capabilities. Gabble should catch
    # this, disco caps, update caps and finally re-process RequestStreams

    # We need remote end's presence for capabilities
    jt.send_remote_presence()

    # Gabble doesn't trust it, so makes a disco
    event = q.expect('stream-iq', query_ns=ns.DISCO_INFO, to='foo@bar.com/Foo')

    jt.send_remote_disco_reply(event.stanza)

    # RequestStreams should now happily complete
    q.expect('dbus-return', method='RequestStreams')

    # Test completed, close the connection

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True


if __name__ == '__main__':
    exec_test(test, timeout=10)

