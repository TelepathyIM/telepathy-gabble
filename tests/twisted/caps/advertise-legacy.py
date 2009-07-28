"""
Test AdvertiseCapabilities.
"""

import dbus

from twisted.words.xish import xpath, domish

from servicetest import EventPattern
from gabbletest import exec_test
from caps_helper import caps_contain, receive_presence_and_ask_caps,\
        check_caps, JINGLE_CAPS
import constants as cs
import ns

def run_test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    # This method call looks wrong, but it's "the other side" of
    # test/twisted/capabilities/legacy-caps.py in MC 5.1 - MC doesn't know
    # how to map Client capabilities into the old Capabilities interface.
    #
    # Also, MC sometimes puts the same channel type in the list twice, so
    # make sure Gabble copes.
    add = [(cs.CHANNEL_TYPE_STREAMED_MEDIA, 2L**32-1),
            (cs.CHANNEL_TYPE_STREAM_TUBE, 2L**32-1),
            (cs.CHANNEL_TYPE_STREAM_TUBE, 2L**32-1)]
    remove = []
    caps = conn.Capabilities.AdvertiseCapabilities(add, remove)
    (disco_response, caps_str, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(disco_response, caps_str, JINGLE_CAPS)

    # Remove all our caps again
    add = []
    remove = [cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.CHANNEL_TYPE_STREAM_TUBE]
    caps = conn.Capabilities.AdvertiseCapabilities(add, remove)
    (disco_response, caps_str, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(disco_response, caps_str, [])

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
    (disco_response, caps_str, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(disco_response, caps_str,
            [ns.GOOGLE_P2P, ns.JINGLE_TRANSPORT_RAWUDP, ns.JINGLE,
                ns.JINGLE_015, ns.GOOGLE_FEAT_VOICE, ns.JINGLE_RTP_AUDIO,
                ns.JINGLE_RTP, ns.JINGLE_015_AUDIO])

    # Remove all our caps again
    add = []
    remove = [cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.CHANNEL_TYPE_STREAM_TUBE]
    caps = conn.Capabilities.AdvertiseCapabilities(add, remove)
    (disco_response, caps_str, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(disco_response, caps_str, [])

    # With AUDIO but no transport, we are only callable via raw UDP, which
    # Google clients cannot do.
    add = [(cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.MEDIA_CAP_AUDIO)]
    remove = []
    caps = conn.Capabilities.AdvertiseCapabilities(add, remove)
    (disco_response, caps_str, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(disco_response, caps_str,
            [ns.JINGLE_TRANSPORT_RAWUDP, ns.JINGLE,
                ns.JINGLE_015, ns.JINGLE_RTP_AUDIO,
                ns.JINGLE_RTP, ns.JINGLE_015_AUDIO])

    # Remove all our caps again
    add = []
    remove = [cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.CHANNEL_TYPE_STREAM_TUBE]
    caps = conn.Capabilities.AdvertiseCapabilities(add, remove)
    (disco_response, caps_str, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(disco_response, caps_str, [])

    # With VIDEO and ICE-UDP only, we are very futuristic indeed.
    # Google clients cannot interop with us.
    add = [(cs.CHANNEL_TYPE_STREAMED_MEDIA,
        cs.MEDIA_CAP_VIDEO | cs.MEDIA_CAP_ICEUDP)]
    remove = []
    caps = conn.Capabilities.AdvertiseCapabilities(add, remove)
    (disco_response, caps_str, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(disco_response, caps_str,
            [ns.JINGLE_TRANSPORT_ICEUDP, ns.JINGLE_TRANSPORT_RAWUDP, ns.JINGLE,
                ns.JINGLE_015, ns.JINGLE_RTP_VIDEO,
                ns.JINGLE_RTP, ns.JINGLE_015_VIDEO])

if __name__ == '__main__':
    exec_test(run_test)
