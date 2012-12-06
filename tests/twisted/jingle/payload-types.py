"""
Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=18918
"""

import dbus

from gabbletest import exec_test
from servicetest import wrap_channel, make_channel_proxy
import jingletest2
import constants as cs

from config import VOIP_ENABLED

if not VOIP_ENABLED:
    print "NOTE: built with --disable-voip"
    raise SystemExit(77)

def test(q, bus, conn, stream):
    jp = jingletest2.JingleProtocol031()
    jt = jingletest2.JingleTest2(jp, conn, q, stream, 'test@localhost',
        'foo@bar.com/Foo')

    self_handle = conn.GetSelfHandle()

    jt.send_presence_and_caps()

    handle = conn.RequestHandles(cs.HT_CONTACT, [jt.peer])[0]
    path = conn.RequestChannel(
        cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.HT_CONTACT, handle, True)

    channel = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia')

    # Test that codec parameters are correctly sent in <parameter> children of
    # <payload-type> rather than as attributes of the latter.

    channel.StreamedMedia.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_AUDIO])

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

    jt.audio_codecs = [ ('GSM', 3, 8000, {'misc': 'other'}) ]
    jt.incoming_call()

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
