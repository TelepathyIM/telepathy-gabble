"""
Test DMTF events in a Call channel
"""

import dbus
from dbus.exceptions import DBusException

from twisted.words.xish import xpath

from gabbletest import exec_test
from servicetest import (
    make_channel_proxy, wrap_content,
    EventPattern, call_async,
    assertEquals, assertContains, assertLength, assertNotEquals
    )
import constants as cs
from jingletest2 import JingleTest2, test_all_dialects
import ns
from config import CHANNEL_TYPE_CALL_ENABLED

if not CHANNEL_TYPE_CALL_ENABLED:
    print "NOTE: built with --disable-channel-type-call"
    raise SystemExit(77)

def run_test(jp, q, bus, conn, stream):
    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]

    # Advertise that we can do new style calls
    conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + ".CallHandler", [
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

    # Ensure a channel that doesn't exist yet.
    ret = conn.Requests.CreateChannel(
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
          cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
          cs.TARGET_HANDLE: remote_handle,
          cs.CALL_INITIAL_AUDIO: True,
        })

    signal = q.expect('dbus-signal', signal='NewChannels',
            predicate=lambda e:
                cs.CHANNEL_TYPE_CONTACT_LIST not in e.args[0][0][1].values())

    chan_path = ret[0]
    chan = bus.get_object(conn.bus_name, chan_path)

    properties = chan.GetAll(cs.CHANNEL_TYPE_CALL,
        dbus_interface=dbus.PROPERTIES_IFACE)

    content = wrap_content(bus.get_object (conn.bus_name, properties["Contents"][0]), ["DTMF", "Media"])

    content_properties = content.GetAll (cs.CALL_CONTENT,
        dbus_interface=dbus.PROPERTIES_IFACE)

    chan.Accept (dbus_interface=cs.CHANNEL_TYPE_CALL)

    cstream = bus.get_object (conn.bus_name, content_properties["Streams"][0])

    send_state = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA, "SendingState",
                             dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (cs.CALL_STREAM_FLOW_STATE_STOPPED, send_state)

    ret = q.expect ('dbus-signal', signal='CallStateChanged')
    assertEquals(cs.CALL_STATE_INITIALISING, ret.args[0])

    recv_state = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA, "ReceivingState",
                             dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (cs.CALL_STREAM_FLOW_STATE_PENDING_START, recv_state)
    cstream.CompleteReceivingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STARTED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    assertEquals (cs.CALL_STREAM_FLOW_STATE_PENDING_START, 
        recv_state)

    # Setup codecs
    md = jt2.get_call_audio_md_dbus()


    [path, _] = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "MediaDescriptionOffer", dbus_interface=dbus.PROPERTIES_IFACE)
    offer = bus.get_object (conn.bus_name, path)
    offer.Accept (md, dbus_interface=cs.CALL_CONTENT_MEDIADESCRIPTION)

    # Add candidates
    candidates = jt2.get_call_remote_transports_dbus ()

    cstream.AddCandidates (candidates,
        dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    ret = q.expect('stream-iq', predicate=jp.action_predicate('session-initiate'))
    jt2.parse_session_initiate(ret.query)

    cstream.FinishInitialCandidates (dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    endpoints = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA,
        "Endpoints", dbus_interface=dbus.PROPERTIES_IFACE)
    endpoint = bus.get_object (conn.bus_name, endpoints[0])

    endpoint.SetEndpointState (1, cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED,
        dbus_interface=cs.CALL_STREAM_ENDPOINT)

    q.expect('dbus-signal', signal='EndpointStateChanged',
        interface=cs.CALL_STREAM_ENDPOINT)

    if jp.is_modern_jingle():
        # The other person's client starts ringing, and tells us so!
        node = jp.SetIq(jt2.peer, jt2.jid, [
            jp.Jingle(jt2.sid, jt2.jid, 'session-info', [
                ('ringing', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
        stream.send(jp.xml(node))

        o = q.expect ('dbus-signal', signal="CallMembersChanged")
        assertEquals({ remote_handle: cs.CALL_MEMBER_FLAG_RINGING }, o.args[0])

    jt2.accept()

    # accept codec offer
    o = q.expect ('dbus-signal', signal='NewMediaDescriptionOffer')

    [path, _ ] = o.args
    md = jt2.get_call_audio_md_dbus()
    offer = bus.get_object (conn.bus_name, path)
    offer.Accept (md, dbus_interface=cs.CALL_CONTENT_MEDIADESCRIPTION)

    # accepted, finally

    # The Stream_ID is specified to be ignored; we use 666 here.
    call_async(q, content.DTMF, 'StartTone', 3)
    q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=['3']),
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                         args = [cs.CALL_SENDING_STATE_PENDING_SEND, 3]),
            EventPattern('dbus-return', method='StartTone'),
            )

    content.Media.AcknowledgeDTMFChange(3, cs.CALL_SENDING_STATE_SENDING)

    call_async(q, content.DTMF, 'StopTone')
    q.expect_many(
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                         args = [cs.CALL_SENDING_STATE_PENDING_STOP_SENDING, 3]),
            EventPattern('dbus-return', method='StopTone'),
            )

    call_async(q, content.Media, 'AcknowledgeDTMFChange', 3,
               cs.CALL_SENDING_STATE_NONE)
    q.expect_many(
        EventPattern('dbus-signal', signal='StoppedTones', args=[True]),
        EventPattern('dbus-return', method='AcknowledgeDTMFChange'),
        )

    call_async(q, content.DTMF, 'MultipleTones', '123')
    q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=['123']),
            EventPattern('dbus-return', method='MultipleTones'),
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                         args = [cs.CALL_SENDING_STATE_PENDING_SEND, 1]),
            )
    content.Media.AcknowledgeDTMFChange(1, cs.CALL_SENDING_STATE_SENDING)

    q.expect('dbus-signal', signal='DTMFChangeRequested',
                         args = [cs.CALL_SENDING_STATE_PENDING_STOP_SENDING, 1])
    content.Media.AcknowledgeDTMFChange(1, cs.CALL_SENDING_STATE_NONE)

    q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=['23']),
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                         args = [cs.CALL_SENDING_STATE_PENDING_SEND, 2]),
            )
    content.Media.AcknowledgeDTMFChange(2, cs.CALL_SENDING_STATE_SENDING)
    q.expect('dbus-signal', signal='DTMFChangeRequested',
                         args = [cs.CALL_SENDING_STATE_PENDING_STOP_SENDING, 2])
    content.Media.AcknowledgeDTMFChange(2, cs.CALL_SENDING_STATE_NONE)

    q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=['3']),
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                         args = [cs.CALL_SENDING_STATE_PENDING_SEND, 3]),
            )
    content.Media.AcknowledgeDTMFChange(3, cs.CALL_SENDING_STATE_SENDING)
    q.expect('dbus-signal', signal='DTMFChangeRequested',
                         args = [cs.CALL_SENDING_STATE_PENDING_STOP_SENDING, 3])
    content.Media.AcknowledgeDTMFChange(3, cs.CALL_SENDING_STATE_NONE)

    q.expect_many(
            EventPattern('dbus-signal', signal='StoppedTones', args=[False])
            )

    call_async(q, content.DTMF, 'MultipleTones',
            '1,1' * 100)
    q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones'),
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                         args = [cs.CALL_SENDING_STATE_PENDING_SEND, 1]),
            EventPattern('dbus-return', method='MultipleTones'),
            )
    call_async(q, content.DTMF, 'MultipleTones', '9')
    q.expect('dbus-error', method='MultipleTones',
            name=cs.SERVICE_BUSY)
    call_async(q, content.DTMF, 'StartTone', 9)
    q.expect('dbus-error', method='StartTone', name=cs.SERVICE_BUSY)

    call_async(q, content.DTMF, 'StopTone')
    q.expect_many(
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                         args = [cs.CALL_SENDING_STATE_PENDING_STOP_SENDING, 1]),
            EventPattern('dbus-return', method='StopTone'),
            )
    call_async(q, content.Media, 'AcknowledgeDTMFChange',
               1, cs.CALL_SENDING_STATE_NONE)
    q.expect_many(
            EventPattern('dbus-signal', signal='StoppedTones', args=[True]),
            EventPattern('dbus-return', method='AcknowledgeDTMFChange'),
            )

    call_async(q, content.DTMF, 'MultipleTones',
            '1w2')
    q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=['1w2']),
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                         args = [cs.CALL_SENDING_STATE_PENDING_SEND, 1]),
            EventPattern('dbus-return', method='MultipleTones'),
            )

    content.Media.AcknowledgeDTMFChange(1, cs.CALL_SENDING_STATE_SENDING)

    q.expect('dbus-signal', signal='DTMFChangeRequested',
                         args = [cs.CALL_SENDING_STATE_PENDING_STOP_SENDING, 1])

    call_async(q, content.Media, 'AcknowledgeDTMFChange',
               1, cs.CALL_SENDING_STATE_NONE)
    ret = q.expect_many(
            EventPattern('dbus-signal', signal='TonesDeferred'),
            EventPattern('dbus-signal', signal='StoppedTones', args=[False]),
            EventPattern('dbus-return', method='AcknowledgeDTMFChange'),
            )
    assertEquals(['2'], ret[0].args);

    chan.Hangup (0, "", "",
        dbus_interface=cs.CHANNEL_TYPE_CALL)

if __name__ == '__main__':
    test_all_dialects(lambda jp, q, bus, conn, stream:
        run_test(jp, q, bus, conn, stream))
