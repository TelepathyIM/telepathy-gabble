"""
Test whether we parse the transport info with multiple contents correctly
"""

from gabbletest import exec_test
from servicetest import ( make_channel_proxy, EventPattern,
    assertEquals, assertNotEquals )
from jingletest2 import *

from config import VOIP_ENABLED

if not VOIP_ENABLED:
    print "NOTE: built with --disable-voip"
    raise SystemExit(77)

def test(q, bus, conn, stream, peer='foo@bar.com/Foo'):
    jp = JingleProtocol031()

    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', peer)
    jt.prepare()

    # Remote end calls us
    jt.incoming_call(audio = "Audio", video = "Video")

    e = q.expect ('dbus-signal', signal='NewSessionHandler')
    handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    handler.Ready()

    events = q.expect_many (
        EventPattern('dbus-signal', signal='NewStreamHandler'),
        EventPattern('dbus-signal', signal='NewStreamHandler')
    )
    for e in events:
        handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')
        handler.Ready([])

    candidate0 = (
        "1.2.3.4", # host
        666, # port
        0, # protocol = TP_MEDIA_STREAM_BASE_PROTO_UDP
        "RTP", # protocol subtype
        "AVP", # profile
        1.0, # preference
        0, # transport type = TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL,
        "username",
        "password" )

    candidate1 = (
        "5.6.7.8", # host
        999, # port
        0, # protocol = TP_MEDIA_STREAM_BASE_PROTO_UDP
        "RTP", # protocol subtype
        "AVP", # profile
        1.0, # preference
        0, # transport type = TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL,
        "username",
        "password" )

    node = jp.SetIq(jt.peer, jt.jid, [
        jp.Jingle(jt.sid, jt.peer, 'transport-info', [
            jp.Content('Audio', 'initiator', 'both',
                transport = jp.TransportGoogleP2P([candidate0])),
            jp.Content('Video', 'initiator', 'both',
                transport = jp.TransportGoogleP2P([candidate1])),
        ] ) ])

    stream.send(jp.xml(node))

    q.expect ('stream-iq', iq_type='result')
    (c0, c1) = q.expect_many(
        EventPattern('dbus-signal',  signal='AddRemoteCandidate'),
        EventPattern('dbus-signal',  signal='AddRemoteCandidate'))

    assertNotEquals(c0.path, c1.path)

    mapping = { 666: candidate0, 999: candidate1}

    # Candidate without component number to compare
    candidate = c0.args[1][0][1:]
    assertEquals(mapping[candidate[1]], candidate)

    candidate = c1.args[1][0][1:]
    assertEquals(mapping[candidate[1]], candidate)

if __name__ == '__main__':
    exec_test(test)
