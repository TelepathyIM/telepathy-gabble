"""
Test DMTF events in a Call channel
"""

import dbus
from dbus.exceptions import DBusException

from twisted.words.xish import xpath

from gabbletest import exec_test
from servicetest import (
    make_channel_proxy, wrap_channel,
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
    chan = wrap_channel(bus.get_object(conn.bus_name, chan_path),
            'Call.DRAFT', ['DTMF'])

    properties = chan.GetAll(cs.CHANNEL_TYPE_CALL,
        dbus_interface=dbus.PROPERTIES_IFACE)

    content = bus.get_object (conn.bus_name, properties["Contents"][0])

    content_properties = content.GetAll (cs.CALL_CONTENT,
        dbus_interface=dbus.PROPERTIES_IFACE)

    chan.Accept (dbus_interface=cs.CHANNEL_TYPE_CALL)

    # Setup codecs
    codecs = jt2.get_call_audio_codecs_dbus()
    # FIXME: update this when my new call branch is merged so that
    # SetCodecs -> UpdateCodecs and a new codec offer appears instead
    # of calling this method.
    content.SetCodecs(codecs, dbus_interface=cs.CALL_CONTENT_IFACE_MEDIA)

    # Add candidates
    candidates = jt2.get_call_remote_transports_dbus ()

    cstream = bus.get_object (conn.bus_name, content_properties["Streams"][0])
    cstream.AddCandidates (candidates,
        dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    ret = q.expect('stream-iq', predicate=jp.action_predicate('session-initiate'))
    jt2.parse_session_initiate(ret.query)

    cstream.CandidatesPrepared (dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    endpoints = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA,
        "Endpoints", dbus_interface=dbus.PROPERTIES_IFACE)
    endpoint = bus.get_object (conn.bus_name, endpoints[0])

    endpoint.SetSelectedCandidate (jt2.get_call_remote_transports_dbus()[0],
        dbus_interface=cs.CALL_STREAM_ENDPOINT)
    endpoint.SetStreamState (cs.MEDIA_STREAM_STATE_CONNECTED,
        dbus_interface=cs.CALL_STREAM_ENDPOINT)

    q.expect('dbus-signal', signal='StreamStateChanged',
        interface=cs.CALL_STREAM_ENDPOINT)

    if jp.is_modern_jingle():
        # The other person's client starts ringing, and tells us so!
        node = jp.SetIq(jt2.peer, jt2.jid, [
            jp.Jingle(jt2.sid, jt2.jid, 'session-info', [
                ('ringing', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
        stream.send(jp.xml(node))

        q.expect ('dbus-signal', signal="CallMembersChanged",
            args = [{ remote_handle: cs.CALL_MEMBER_FLAG_RINGING }, []])

    jt2.accept()

    # accept codec offer
    o = q.expect ('dbus-signal', signal='NewCodecOffer')

    [path, _ ] = o.args
    codecs = jt2.get_call_audio_codecs_dbus()
    offer = bus.get_object (conn.bus_name, path)
    offer.Accept (codecs, dbus_interface=cs.CALL_CONTENT_CODECOFFER)

    # accepted, finally

    # The Stream_ID is specified to be ignored; we use 666 here.
    call_async(q, chan.DTMF, 'StartTone', 666, 3)
    q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=['3'],
                path=chan_path),
            EventPattern('dbus-return', method='StartTone'),
            )

    call_async(q, chan.DTMF, 'StopTone', 666)
    q.expect_many(
            EventPattern('dbus-signal', signal='StoppedTones', args=[True],
                path=chan_path),
            EventPattern('dbus-return', method='StopTone'),
            )

    call_async(q, chan.DTMF, 'MultipleTones', '123')
    q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=['123'],
                path=chan_path),
            EventPattern('dbus-return', method='MultipleTones'),
            )
    q.expect_many(
            EventPattern('dbus-signal', signal='StoppedTones', args=[False],
                path=chan_path),
            )

    # This is technically a race condition, but this dialstring is almost
    # certainly long enough that the Python script will win the race, i.e.
    # cancel before Gabble processes the whole dialstring.
    call_async(q, chan.DTMF, 'MultipleTones',
            '1,1' * 100)
    q.expect('dbus-return', method='MultipleTones')
    call_async(q, chan.DTMF, 'MultipleTones', '9')
    q.expect('dbus-error', method='MultipleTones',
            name=cs.SERVICE_BUSY)
    call_async(q, chan.DTMF, 'StartTone', 666, 9)
    q.expect('dbus-error', method='StartTone', name=cs.SERVICE_BUSY)
    call_async(q, chan.DTMF, 'StopTone', 666)
    q.expect_many(
            EventPattern('dbus-signal', signal='StoppedTones', args=[True],
                path=chan_path),
            EventPattern('dbus-return', method='StopTone'),
            )

    chan.Hangup (0, "", "",
        dbus_interface=cs.CHANNEL_TYPE_CALL)

if __name__ == '__main__':
    test_all_dialects(lambda jp, q, bus, conn, stream:
        run_test(jp, q, bus, conn, stream))
