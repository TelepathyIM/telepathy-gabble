"""
Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=18918
"""

import dbus

from gabbletest import exec_test, sync_stream
from servicetest import make_channel_proxy
import jingletest
import constants as cs

from twisted.words.xish import domish

def test(q, bus, conn, stream):
    jt = jingletest.JingleTest(stream, 'test@localhost', 'foo@bar.com/Foo')

    # Connecting
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    self_handle = conn.GetSelfHandle()

    # We need remote end's presence for capabilities
    jt.send_remote_presence()

    # Gabble doesn't trust it, so makes a disco
    event = q.expect('stream-iq', query_ns='http://jabber.org/protocol/disco#info',
             to='foo@bar.com/Foo')

    jt.send_remote_disco_reply(event.stanza)

    # Force Gabble to process the caps before calling RequestChannel
    sync_stream(q, stream)

    handle = conn.RequestHandles(cs.HT_CONTACT, [jt.remote_jid])[0]
    path = conn.RequestChannel(
        cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.HT_CONTACT, handle, True)

    channel = bus.get_object(conn.bus_name, path)
    signalling_iface = make_channel_proxy(conn, path, 'Channel.Interface.MediaSignalling')
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')
    group_iface = make_channel_proxy(conn, path, 'Channel.Interface.Group')

    # FIXME: Hack to make sure the disco info has been processed - we need to
    # send Gabble some XML that will cause an event when processed, and
    # wait for that event (until
    # https://bugs.freedesktop.org/show_bug.cgi?id=15769 is fixed)
    el = domish.Element(('jabber:client', 'presence'))
    el['from'] = 'bob@example.com/Bar'
    stream.send(el.toXml())
    q.expect('dbus-signal', signal='PresenceUpdate')
    # OK, now we can continue. End of hack


    # Test that codec parameters are correctly sent in <parameter> children of
    # <payload-type> rather than as attributes of the latter.

    media_iface.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_AUDIO])

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    stream_id = e.args[1]

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')
    stream_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())

    codecs = dbus.Array( [ (96, 'speex', 0, 16000, 0, {'vbr': 'on'}) ],
                         signature='(usuuua{ss})')
    stream_handler.Ready(codecs)
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    e = q.expect('stream-iq')
    content = list(e.query.elements())[0]
    assert content.name == 'content'
    for child in content.elements():
        if child.name == 'description':
            description = child
            break
    assert description is not None

    # there should be one <payload-type> tag for speex:
    assert len(list(description.elements())) == 1
    payload_type = list(description.elements())[0]
    assert payload_type.name == 'payload-type'
    assert payload_type['name'] == 'speex'

    # the vbr parameter should not be an attribute on the <payload-type>, but
    # a child <parameter/> tag
    assert 'vbr' not in payload_type.attributes
    assert len(list(payload_type.elements())) == 1
    parameter = list(payload_type.elements())[0]
    assert parameter.name == 'parameter'
    assert parameter['name'] == 'vbr'
    assert parameter['value'] == 'on'

    channel.Close()


    # Test that codec parameters are correctly extracted from <parameter>
    # children of <payload-type> rather than from attributes of the latter.

    jt.incoming_call({'misc': 'other'})

    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    stream_id = e.args[1]

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')
    stream_handler.Ready( dbus.Array( [], signature='(usuuua{ss})'))

    e = q.expect('dbus-signal', signal='SetRemoteCodecs')
    for codec in e.args[0]:
        id, name, type, rate, channels, parameters = codec
        assert len(parameters) == 1, parameters
        assert parameters['misc'] == 'other', parameters

if __name__ == '__main__':
    exec_test(test)
