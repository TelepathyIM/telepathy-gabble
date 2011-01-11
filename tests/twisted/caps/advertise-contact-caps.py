"""
Test UpdateCapabilities.
"""

import dbus

from twisted.words.xish import xpath, domish

from servicetest import EventPattern
from gabbletest import exec_test, sync_stream
from caps_helper import caps_contain, receive_presence_and_ask_caps, \
        FIXED_CAPS, JINGLE_CAPS, VARIABLE_CAPS, check_caps, disco_caps
import constants as cs
import ns

def noop_presence_update(q, stream):
    # At the moment Gabble does not optimize away presence updates that
    # have no effect. When it does, we can forbid those events here.

    #events = [EventPattern('stream-presence')]
    #q.forbid_events(events)
    sync_stream(q, stream)
    #q.unforbid_events(events)

JINGLE_CAPS_EXCEPT_GVIDEO = [n for n in JINGLE_CAPS
    if n != ns.GOOGLE_FEAT_VIDEO]

def run_test(q, bus, conn, stream,
        media_channel_type, media_interface,
        initial_audio, initial_video):
    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + '.AbiWord', [
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.STREAM_TUBE_SERVICE: 'x-abiword' },
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.STREAM_TUBE_SERVICE: 'x-abiword' },
        ], []),
        ])

    conn.Connect()

    _, initial_presence = q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged',
                args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
            EventPattern('stream-presence'),
            )
    (disco_response, namespaces) = disco_caps(q, stream, initial_presence)
    check_caps(namespaces, [ns.TUBES + '/stream#x-abiword'])

    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + '.AbiWord', [
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.STREAM_TUBE_SERVICE: 'x-abiword' },
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.STREAM_TUBE_SERVICE: 'x-abiword' },
        ], []),
        (cs.CLIENT + '.KCall', [
        { cs.CHANNEL_TYPE: media_channel_type },
        { cs.CHANNEL_TYPE: media_channel_type,
            initial_audio: True},
        { cs.CHANNEL_TYPE: media_channel_type,
            initial_video: True},
        ], [
            media_interface + '/gtalk-p2p',
            media_interface + '/ice-udp',
            media_interface + '/video/h264',
            ]),
        ])
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces, JINGLE_CAPS + [ns.TUBES + '/stream#x-abiword'])

    # Removing our H264 codec removes our ability to do Google Video
    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + '.KCall', [
        { cs.CHANNEL_TYPE: media_channel_type },
        { cs.CHANNEL_TYPE: media_channel_type,
            initial_audio: True},
        { cs.CHANNEL_TYPE: media_channel_type,
            initial_video: True},
        ], [
            media_interface + '/gtalk-p2p',
            media_interface + '/ice-udp',
            ]),
        ])
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces,
            JINGLE_CAPS_EXCEPT_GVIDEO + [ns.TUBES + '/stream#x-abiword'])

    # Remove AbiWord's caps
    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + '.AbiWord', [], []),
        ])
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces, JINGLE_CAPS_EXCEPT_GVIDEO)

    # Remove KCall's caps too
    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + '.KCall', [], []),
        ])
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces, [])

    # If AbiWord claims that it can do MediaSignalling things on its Tubes
    # channels, then it's wrong, and that still doesn't make us callable.
    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + '.AbiWord', [
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.STREAM_TUBE_SERVICE: 'x-abiword' },
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.STREAM_TUBE_SERVICE: 'x-abiword' },
        ], [
            media_interface + '/gtalk-p2p',
            media_interface + '/ice-udp',
            media_interface + '/video/h264',
            ]),
        ])
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces, [ns.TUBES + '/stream#x-abiword'])

    # Remove the broken version of AbiWord's caps
    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + '.AbiWord', [], []),
        ])
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces, [])

    # Add caps selectively. Here we're callable, but not via ICE-UDP, and not
    # with video.
    #
    # (Jingle and raw UDP need no special client support, so are automatically
    # enabled whenever we can do audio or video.)
    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + '.KCall', [
        { cs.CHANNEL_TYPE: media_channel_type },
        { cs.CHANNEL_TYPE: media_channel_type,
            initial_audio: True},
        ], [media_interface + '/gtalk-p2p']),
        ])
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces,
            [ns.GOOGLE_P2P, ns.JINGLE_TRANSPORT_RAWUDP, ns.JINGLE,
                ns.JINGLE_015, ns.GOOGLE_FEAT_VOICE, ns.JINGLE_RTP_AUDIO,
                ns.JINGLE_RTP, ns.JINGLE_015_AUDIO])

    # With AUDIO but no transport, we are only callable via raw UDP, which
    # Google clients cannot do.
    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + '.KCall', [
        { cs.CHANNEL_TYPE: media_channel_type },
        { cs.CHANNEL_TYPE: media_channel_type,
            initial_audio: True},
        ], [])
        ])
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces,
            [ns.JINGLE_TRANSPORT_RAWUDP, ns.JINGLE,
                ns.JINGLE_015, ns.JINGLE_RTP_AUDIO,
                ns.JINGLE_RTP, ns.JINGLE_015_AUDIO])

    # With VIDEO and ICE-UDP only, we are very futuristic indeed.
    # Google clients cannot interop with us.
    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + '.KCall', [
        { cs.CHANNEL_TYPE: media_channel_type },
        { cs.CHANNEL_TYPE: media_channel_type,
            initial_video: True},
        ], [
            media_interface + '/ice-udp',
            media_interface + '/video/theora',
            ]),
        ])
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces,
            [ns.JINGLE_TRANSPORT_ICEUDP, ns.JINGLE_TRANSPORT_RAWUDP, ns.JINGLE,
                ns.JINGLE_015, ns.JINGLE_RTP_VIDEO,
                ns.JINGLE_RTP, ns.JINGLE_015_VIDEO])

    # Remove KCall to simplify subsequent checks
    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + '.KCall', [], []),
        ])
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces, [])

    # Support file transfer
    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + '.FileReceiver', [{
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            }], []),
        ])
    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces, [ns.FILE_TRANSFER])

def run_mixed_test (q, bus, conn, stream):
    conn.Connect()

    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + '.SquareWheel', [
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.INITIAL_AUDIO: True},
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.INITIAL_VIDEO: True},
        ], [
            cs.CHANNEL_IFACE_MEDIA_SIGNALLING + '/gtalk-p2p',
            cs.CHANNEL_IFACE_MEDIA_SIGNALLING + '/ice-udp',
            cs.CHANNEL_IFACE_MEDIA_SIGNALLING + '/video/h264',
            ]),
        (cs.CLIENT + '.FlyingCar', [
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
            cs.CALL_INITIAL_AUDIO: True},
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
            cs.CALL_INITIAL_VIDEO: True},
        ], [
            cs.CHANNEL_TYPE_CALL + '/gtalk-p2p',
            cs.CHANNEL_TYPE_CALL + '/ice-udp',
            cs.CHANNEL_TYPE_CALL + '/video/h264',
            ]),
        ])

    (disco_response, namespaces, _) = receive_presence_and_ask_caps(q, stream,
            False)
    check_caps(namespaces, JINGLE_CAPS)

if __name__ == '__main__':
    exec_test(lambda q, b, c, s:
        run_test (q, b, c, s,
            cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.CHANNEL_IFACE_MEDIA_SIGNALLING,
            cs.INITIAL_AUDIO, cs.INITIAL_VIDEO), do_connect=False)

    exec_test(lambda q, b, c, s:
        run_test (q, b, c, s,
            cs.CHANNEL_TYPE_CALL, cs.CHANNEL_TYPE_CALL,
            cs.CALL_INITIAL_AUDIO, cs.CALL_INITIAL_VIDEO), do_connect=False)

    exec_test(run_mixed_test, do_connect=False)
