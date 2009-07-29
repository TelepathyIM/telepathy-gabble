"""
Test SetSelfCapabilities.
"""

import dbus

from twisted.words.xish import xpath, domish

from servicetest import EventPattern
from gabbletest import exec_test, sync_stream
from caps_helper import caps_contain, receive_presence_and_ask_caps, \
        FIXED_CAPS, JINGLE_CAPS, VARIABLE_CAPS, check_caps
import constants as cs
import ns

def noop_presence_update(q, stream):
    # At the moment Gabble does not optimize away presence updates that
    # have no effect. When it does, we can forbid those events here.

    #events = [EventPattern('stream-presence')]
    #q.forbid_events(events)
    sync_stream(q, stream)
    #q.unforbid_events(events)

def run_test(q, bus, conn, stream):
    conn.Connect()

    _, initial_presence = q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged',
                args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
            EventPattern('stream-presence'),
            )

    # This method call looks wrong, but it's "the other side" of
    # test/twisted/capabilities/draft-1.py in MC 5.1 - MC doesn't know
    # how to map Client capabilities into the old Capabilities interface.
    add = [(cs.CHANNEL_TYPE_STREAMED_MEDIA, 2L**32-1),
            (cs.CHANNEL_TYPE_STREAM_TUBE, 2L**32-1)]
    remove = []
    caps = conn.Capabilities.AdvertiseCapabilities(add, remove)
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces, JINGLE_CAPS)
    # Immediately afterwards, we get SetSelfCapabilities, for which a
    # more comprehensive test exists in tube-caps.py.
    conn.ContactCapabilities.SetSelfCapabilities([
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.STREAM_TUBE_SERVICE: 'x-abiword' },
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.STREAM_TUBE_SERVICE: 'x-abiword' },
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA },
        ])
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces, JINGLE_CAPS + [ns.TUBES + '/stream#x-abiword'])

    # Remove all our caps again
    add = []
    remove = [cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.CHANNEL_TYPE_STREAM_TUBE]
    caps = conn.Capabilities.AdvertiseCapabilities(add, remove)
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces, [])
    # the call to SSC has no effect here
    conn.ContactCapabilities.SetSelfCapabilities([])
    noop_presence_update(q, stream)

    # Add caps selectively (i.e. what a client that actually understood the
    # old Capabilities interface would do). With AUDIO and GTALK_P2P, we're
    # callable, but not via ICE-UDP, and not with video.
    #
    # (Jingle and raw UDP need no special client support, so are automatically
    # enabled whenever we can do audio or video.)
    add = [(cs.CHANNEL_TYPE_STREAMED_MEDIA,
        cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_GTALKP2P)]
    remove = []
    caps = conn.Capabilities.AdvertiseCapabilities(add, remove)
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces,
            [ns.GOOGLE_P2P, ns.JINGLE_TRANSPORT_RAWUDP, ns.JINGLE,
                ns.JINGLE_015, ns.GOOGLE_FEAT_VOICE, ns.JINGLE_RTP_AUDIO,
                ns.JINGLE_RTP, ns.JINGLE_015_AUDIO])
    # the call to SSC has no effect here
    conn.ContactCapabilities.SetSelfCapabilities([
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA },
        ])
    noop_presence_update(q, stream)

    # Remove all our caps again
    add = []
    remove = [cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.CHANNEL_TYPE_STREAM_TUBE]
    caps = conn.Capabilities.AdvertiseCapabilities(add, remove)
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces, [])
    # the call to SSC has no effect here
    conn.ContactCapabilities.SetSelfCapabilities([])
    noop_presence_update(q, stream)

    # With AUDIO but no transport, we are only callable via raw UDP, which
    # Google clients cannot do.
    add = [(cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.MEDIA_CAP_AUDIO)]
    remove = []
    caps = conn.Capabilities.AdvertiseCapabilities(add, remove)
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces,
            [ns.JINGLE_TRANSPORT_RAWUDP, ns.JINGLE,
                ns.JINGLE_015, ns.JINGLE_RTP_AUDIO,
                ns.JINGLE_RTP, ns.JINGLE_015_AUDIO])
    # the call to SSC has no effect here
    conn.ContactCapabilities.SetSelfCapabilities([
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA },
        ])
    noop_presence_update(q, stream)

    # Remove all our caps again
    add = []
    remove = [cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.CHANNEL_TYPE_STREAM_TUBE]
    caps = conn.Capabilities.AdvertiseCapabilities(add, remove)
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces, [])
    # the call to SSC has no effect here
    conn.ContactCapabilities.SetSelfCapabilities([])
    noop_presence_update(q, stream)

    # With VIDEO and ICE-UDP only, we are very futuristic indeed.
    # Google clients cannot interop with us.
    add = [(cs.CHANNEL_TYPE_STREAMED_MEDIA,
        cs.MEDIA_CAP_VIDEO | cs.MEDIA_CAP_ICEUDP)]
    remove = []
    caps = conn.Capabilities.AdvertiseCapabilities(add, remove)
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces,
            [ns.JINGLE_TRANSPORT_ICEUDP, ns.JINGLE_TRANSPORT_RAWUDP, ns.JINGLE,
                ns.JINGLE_015, ns.JINGLE_RTP_VIDEO,
                ns.JINGLE_RTP, ns.JINGLE_015_VIDEO])
    # the call to SSC has no effect here
    conn.ContactCapabilities.SetSelfCapabilities([
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA },
        ])
    noop_presence_update(q, stream)

if __name__ == '__main__':
    exec_test(run_test)
