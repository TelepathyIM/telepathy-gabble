"""
Test several different permutations of features that should a client audio
and/or video capable
"""

from gabbletest import exec_test, make_presence, sync_stream
from servicetest import assertContains
import constants as cs
import ns
from caps_helper import presence_and_disco, compute_caps_hash

client = 'http://telepathy.freedesktop.org/fake-client'
caps = { 'node': client, 'ver':  "dummy", 'hash': 'sha-1' }
all_transports = [
    ns.JINGLE_TRANSPORT_ICEUDP,
    ns.JINGLE_TRANSPORT_RAWUDP,
    ns.GOOGLE_P2P
]

def test_caps(q, conn, stream, contact, features, audio, video, google=False):
    caps['ver'] = compute_caps_hash ([], features, {})

    h = presence_and_disco(q, conn, stream, contact, True,
        client, caps, features)
    event = q.expect('dbus-signal', signal='CapabilitiesChanged')

    cflags = 0
    if audio:
      cflags |= cs.MEDIA_CAP_AUDIO
    if video:
      cflags |= cs.MEDIA_CAP_VIDEO

    # If the contact can only do one of audio or video, or uses a Google
    # client, they'll have the ImmutableStreams cap.
    if cflags < (cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_VIDEO) or google:
        cflags |= cs.MEDIA_CAP_IMMUTABLE_STREAMS

    assertContains((h, cs.CHANNEL_TYPE_STREAMED_MEDIA, 3, cflags),
        conn.Capabilities.GetCapabilities([h]))

def test_all_transports(q, conn, stream, contact, features, audio, video):
    for t in all_transports:
        test_caps(q, conn, stream, contact, features + [t] , audio, video)
        contact += "a"

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # Fully capable jingle clients with one transport each
    features = [ ns.JINGLE_RTP, ns.JINGLE_RTP_AUDIO, ns.JINGLE_RTP_VIDEO ]
    test_all_transports(q, conn, stream, "full@a", features, True, True)

    # video capable jingle clients with one transport each
    features = [ ns.JINGLE_RTP, ns.JINGLE_RTP_VIDEO ]
    test_all_transports (q, conn, stream, "video@a", features, False, True)

    # audio capable jingle clients with one transport each
    features = [ ns.JINGLE_RTP, ns.JINGLE_RTP_AUDIO ]
    test_all_transports(q, conn, stream, "audio@a", features, True, False)

    # old jingle client fully capable
    features = [ ns.JINGLE_015, ns.JINGLE_015_AUDIO, ns.JINGLE_015_VIDEO ]
    test_all_transports(q, conn, stream, "oldfull@a", features, True, True)

    # old jingle client video capable
    features = [ ns.JINGLE_015, ns.JINGLE_015_VIDEO ]
    test_all_transports(q, conn, stream, "oldvideo@a", features, False, True)

    # old jingle client audio capable
    features = [ ns.JINGLE_015, ns.JINGLE_015_AUDIO ]
    test_all_transports(q, conn, stream, "oldaudio@a", features, True, False)

    # Google media doesn't need a transport at all
    features = [ ns.GOOGLE_FEAT_VOICE, ns.GOOGLE_FEAT_VIDEO ]
    test_caps(q, conn, stream, "full@google", features, True, True,
        google=True)

    # Google video only
    features = [ ns.GOOGLE_FEAT_VIDEO ]
    test_caps(q, conn, stream, "video@google", features, False, True,
        google=True)

    # Google audio only
    features = [ ns.GOOGLE_FEAT_VOICE ]
    test_caps(q, conn, stream, "audio@google", features, True, False,
        google=True)


if __name__ == '__main__':
    exec_test(test)
