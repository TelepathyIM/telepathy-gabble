"""
Test DMTF events in a Call channel
"""

import dbus
from dbus.exceptions import DBusException

from twisted.words.xish import xpath

from servicetest import (
    make_channel_proxy, wrap_content,
    EventPattern, call_async,
    assertEquals, assertContains, assertLength, assertNotEquals
    )
import constants as cs
from jingletest2 import JingleTest2, test_all_dialects
import ns
from config import CHANNEL_TYPE_CALL_ENABLED
import callutils as cu

if not CHANNEL_TYPE_CALL_ENABLED:
    print "NOTE: built with --disable-channel-type-call"
    raise SystemExit(77)

def run_test(jp, q, bus, conn, stream):
    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]

    cu.advertise_call(conn)

    # Ensure a channel that doesn't exist yet.
    try:
        ret = conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: remote_handle,
              cs.CALL_INITIAL_AUDIO: False,
              cs.CALL_INITIAL_VIDEO: True,
            })
    except DBusException as e:
        assertEquals (jp.can_do_video_only(), False)
        assertEquals (e.get_dbus_name(),
                'org.freedesktop.Telepathy.Error.NotCapable')
        return

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
    md = jt2.get_call_video_md_dbus()


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
    md = jt2.get_call_video_md_dbus()
    offer = bus.get_object (conn.bus_name, path)
    offer.Accept (md, dbus_interface=cs.CALL_CONTENT_MEDIADESCRIPTION)

    # accepted, finally

    # Check the DTMF method does not exist
    call_async(q, content.DTMF, 'StartTone', 3)
    q.expect('dbus-error', method='StartTone')
    
    chan.Hangup (0, "", "",
        dbus_interface=cs.CHANNEL_TYPE_CALL)

if __name__ == '__main__':
    test_all_dialects(lambda jp, q, bus, conn, stream:
        run_test(jp, q, bus, conn, stream))
