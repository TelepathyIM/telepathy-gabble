"""
Test several different permutations of features that should a client audio
and/or video capable
"""

from functools import partial
from itertools import permutations

from gabbletest import exec_test, make_presence, sync_stream
from servicetest import (
    assertContains, assertDoesNotContain, assertEquals, EventPattern,
    make_channel_proxy
    )
import constants as cs
import ns
from caps_helper import presence_and_disco, compute_caps_hash, send_presence
from jingle.jingletest2 import JingleTest2, JingleProtocol031
from call_helper import CallTest

from config import VOIP_ENABLED

if not VOIP_ENABLED:
    print "NOTE: built with --disable-voip"
    raise SystemExit(77)

client = 'http://telepathy.freedesktop.org/fake-client'
caps = { 'node': client, 'ver':  "dummy", 'hash': 'sha-1' }
all_transports = [
    ns.JINGLE_TRANSPORT_ICEUDP,
    ns.JINGLE_TRANSPORT_RAWUDP,
    ns.GOOGLE_P2P
]

def check_contact_caps (caps, channel_type, expected_media_caps):

    [media_caps] =  [ c
        for c in caps
            if c[0][cs.CHANNEL_TYPE] == channel_type
    ]

    assertEquals (expected_media_caps, media_caps[1])

def test_caps(q, conn, stream, contact, features, audio, video, google=False):
    caps['ver'] = compute_caps_hash ([], features, {})

    h = presence_and_disco(q, conn, stream, contact, True,
        client, caps, features)

    cflags = 0
    stream_expected_media_caps = []
    call_expected_media_caps = []

    if audio:
      cflags |= cs.MEDIA_CAP_AUDIO
      stream_expected_media_caps.append (cs.INITIAL_AUDIO)
      call_expected_media_caps.append (cs.CALL_INITIAL_AUDIO)
      call_expected_media_caps.append (cs.CALL_INITIAL_AUDIO_NAME)
    if video:
      cflags |= cs.MEDIA_CAP_VIDEO
      stream_expected_media_caps.append (cs.INITIAL_VIDEO)
      call_expected_media_caps.append (cs.CALL_INITIAL_VIDEO)
      call_expected_media_caps.append (cs.CALL_INITIAL_VIDEO_NAME)

    # If the contact can only do one of audio or video, or uses a Google
    # client, they'll have the ImmutableStreams cap.
    if cflags < (cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_VIDEO) or google:
        cflags |= cs.MEDIA_CAP_IMMUTABLE_STREAMS
        stream_expected_media_caps.append(cs.IMMUTABLE_STREAMS)
    else:
        call_expected_media_caps.append(cs.CALL_MUTABLE_CONTENTS)

    event, = q.expect_many(
            EventPattern('dbus-signal', signal='ContactCapabilitiesChanged')
        )

    # Check Contact capabilities for streamed media
    assertEquals(len(event.args), 1)
    assertEquals (event.args[0],
        conn.ContactCapabilities.GetContactCapabilities([h]))

    check_contact_caps (event.args[0][h],
        cs.CHANNEL_TYPE_STREAMED_MEDIA, stream_expected_media_caps)

    check_contact_caps (event.args[0][h],
        cs.CHANNEL_TYPE_CALL, call_expected_media_caps)

def test_all_transports(q, conn, stream, contact, features, audio, video):
    for t in all_transports:
        test_caps(q, conn, stream, contact, features + [t] , audio, video)
        contact += "a"

def test(q, bus, conn, stream):
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

def test_prefer_phones(q, bus, conn, stream, expect_disco):
    cat = 'cat@windowsill'

    # This needs to be done once per connection
    jp = JingleProtocol031()
    JingleTest2(jp, conn, q, stream, 'test@localhost',
            cat).prepare()

    def sign_in_a_cat(jid, identities, show=None):
        caps['ver'] = compute_caps_hash(identities, features, {})

        presence_and_disco(q, conn, stream, jid, expect_disco, client, caps, features,
            identities=identities, initial=False, show=show)
        # Make sure Gabble's got the caps
        sync_stream(q, stream)

    def make_call(expected_recipient):

        class MyTest(CallTest):
            PEER_JID = expected_recipient

            def check_session_initiate_iq(self, e):
                assertEquals(expected_recipient, e.to)

            def prepare(self):
                # Don't do the preparation step: we did that already
                pass

        test = MyTest(jp, q, bus, conn, stream, incoming=False, params={})
        test.run()

    features = [ ns.JINGLE_RTP, ns.JINGLE_RTP_AUDIO, ns.JINGLE_RTP_VIDEO
               ] + all_transports

    # My cat is signed in with their laptop (which is available)...
    laptop_jid = 'cat@windowsill/Laptop'
    sign_in_a_cat(laptop_jid, ['client/pc//clocks'])

    # ...and a web client, which is away.
    cloud_jid = 'cat@windowsill/Cloud'
    sign_in_a_cat(cloud_jid, ['client/web//zomg'], show='away')

    # The laptop is more available, so the call should go there.
    make_call(expected_recipient=laptop_jid)

    # But if my cat signs in with a phone, also set to away...
    phone_jid = 'cat@windowsill/Fido'
    sign_in_a_cat(phone_jid, ['client/phone//mars rover'], show='away')

    # ...then calls should go there, even though the laptop is more available.
    make_call(expected_recipient=phone_jid)

def test_google_caps(q, bus, conn, stream):
    i = 1

    # we want to make sure all permutations of voice-v1 and video-v1
    # result in the correct caps, so let's do exactly that.
    for j in (1, 2):
        for ext_set in permutations(['voice-v1', 'video-v1'], j):
            jid = 'larry%s@page/mountainview' % i
            i += 1

            # order of these ext values shouldn't matter
            gcaps = { 'node': 'blahblahthiskeepsonchanging',
                      'ver':  '1.1',
                      'ext': ' '.join(ext_set) }

            handle = conn.RequestHandles(cs.HT_CONTACT, [jid])[0]

            send_presence(q, conn, stream, jid, gcaps, initial=True)

            e = q.expect('dbus-signal', signal='ContactCapabilitiesChanged',
                         predicate=lambda e: handle in e.args[0])

            assertEquals(1, len(e.args[0]))
            rccs = e.args[0][handle]

            found = False
            for fixed, allowed in rccs:
                if fixed[cs.CHANNEL_TYPE] != cs.CHANNEL_TYPE_CALL:
                    continue

                # we should only have InitialAudio or InitialVideo if
                # voice-v1 or video-v1 is present respectively
                for a, b in [('voice-v1' in ext_set, cs.CALL_INITIAL_AUDIO),
                             ('video-v1' in ext_set, cs.CALL_INITIAL_VIDEO)]:
                    if a:
                        assertContains(b, allowed)
                    else:
                        assertDoesNotContain(b, allowed)

                found = True

            assert found

if __name__ == '__main__':
    exec_test(test)

    exec_test(partial(test_prefer_phones, expect_disco=True))
    # And again, this time pulling the caps from the cache. This tests that the
    # quirk is cached!
    exec_test(partial(test_prefer_phones, expect_disco=False))

    exec_test(test_google_caps)
