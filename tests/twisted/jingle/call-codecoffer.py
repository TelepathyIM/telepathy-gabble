"""
Testing different methods related to the CodecOffer interface.
"""

import dbus
from dbus.exceptions import DBusException

from servicetest import (EventPattern,
    assertEquals, assertContains, assertLength, assertNotEquals
    )
import ns
import constants as cs

from jingletest2 import JingleTest2, test_dialects, JingleProtocol031

from config import CHANNEL_TYPE_CALL_ENABLED

if not CHANNEL_TYPE_CALL_ENABLED:
    print "NOTE: built with --disable-channel-type-call"
    raise SystemExit(77)

def check_offer (bus, conn, content):
    [path, handle, codecs] = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "CodecOffer", dbus_interface=dbus.PROPERTIES_IFACE)

    assertNotEquals ("/", path)

    offer = bus.get_object (conn.bus_name, path)
    codecs_property = offer.Get (cs.CALL_CONTENT_CODECOFFER,
        "RemoteContactCodecs", dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (codecs, codecs_property)

def accept_offer (q, bus, conn, self_handle, remote_handle,
        content, codecs, offer_path = None,
        codecs_changed = True):
    [path, _, _] = content.Get (cs.CALL_CONTENT_IFACE_MEDIA,
                "CodecOffer", dbus_interface=dbus.PROPERTIES_IFACE)

    offer = bus.get_object (conn.bus_name, path)

    offer.Accept (codecs, dbus_interface=cs.CALL_CONTENT_CODECOFFER)

    current_codecs = content.Get (cs.CALL_CONTENT_IFACE_MEDIA,
                "ContactCodecMap", dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (codecs,  current_codecs[self_handle])

    if codecs_changed:
        o = q.expect ('dbus-signal', signal='CodecsChanged')

        assertEquals ([{ self_handle: codecs, remote_handle: codecs}, []],
            o.args)

def reject_offer (q, bus, conn,
        content, codecs, offer_path = None):
    [path, _, _] = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "CodecOffer", dbus_interface=dbus.PROPERTIES_IFACE)

    offer = bus.get_object (conn.bus_name, path)

    offer.Reject (dbus_interface=cs.CALL_CONTENT_CODECOFFER)

def update_codecs(jt2):
    contents = jt2.generate_contents()

    node = jt2.jp.SetIq(jt2.peer, jt2.jid, [
        jt2.jp.Jingle(jt2.sid, jt2.peer, 'description-info', contents),
        ])
    jt2.stream.send(jt2.jp.xml(node))

def prepare_test(jp, q, bus, conn, stream):
    remote_jid = 'foo@bar.com/Foo'
    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)

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

    return remote_jid, jt2, self_handle, remote_handle

def try_to_access_old_offer(conn, path):
    try:
        offer = bus.get_object (conn.bus_name, path)
        ret = offer.GetAll (cs.CALL_CONTENT_CODECOFFER,
            dbus_interface=dbus.PROPERTIES_IFACE)
    except Exception, e:
        pass
    else:
        assert False, 'Offer still exists'

def test_incoming(jp, q, bus, conn, stream):
    remote_jid, jt2, self_handle, remote_handle = prepare_test(jp, q, bus, conn, stream)

    jt2.incoming_call()

    ret = q.expect_many(EventPattern('dbus-signal', signal='NewChannels',
            predicate=lambda e:
                cs.CHANNEL_TYPE_CALL in e.args[0][0][1].values()),
        EventPattern('dbus-signal', signal='NewCodecOffer'))

    chan = bus.get_object(conn.bus_name, ret[0].args[0][0][0])

    properties = chan.GetAll(cs.CHANNEL_TYPE_CALL,
        dbus_interface=dbus.PROPERTIES_IFACE)

    content = bus.get_object (conn.bus_name, properties["Contents"][0])

    codecs = jt2.get_call_audio_codecs_dbus()
    check_offer(bus, conn, content)
    accept_offer(q, bus, conn, self_handle, remote_handle,
        content, codecs)

    update_codecs(jt2)
    signal = q.expect('dbus-signal', signal='NewCodecOffer')
    check_offer(bus, conn, content)
    reject_offer(q, bus, conn, content, codecs)

    update_codecs(jt2)
    signal = q.expect('dbus-signal', signal='NewCodecOffer')
    check_offer(bus, conn, content)
    accept_offer(q, bus, conn, self_handle, remote_handle,
        content, codecs, codecs_changed = False)

    update_codecs(jt2)
    signal = q.expect('dbus-signal', signal='NewCodecOffer')
    check_offer(bus, conn, content)

    [path, _, _] = content.Get (cs.CALL_CONTENT_IFACE_MEDIA,
                "CodecOffer", dbus_interface=dbus.PROPERTIES_IFACE)

    chan.Close(dbus_interface=cs.CHANNEL)
    signal = q.expect('dbus-signal', signal='ChannelClosed')

    try_to_access_old_offer(conn, path)

    try:
        ret = conn.GetAll (cs.CONN, dbus_interface=dbus.PROPERTIES_IFACE)
    except Exception, e:
        print 'Gabble probably crashed'
        raise e
    else:
        # depending on the age of our telepathy-glib, we have at least
        # SelfHandle, and might also have Interfaces and Status
        assert len(ret) > 0

def test_outgoing(jp, q, bus, conn, stream):
    remote_jid, jt2, self_handle, remote_handle = prepare_test(jp, q, bus, conn, stream)

    # make a new outgoing call
    conn.CreateChannel({
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
            cs.TARGET_HANDLE: remote_handle,
            cs.CALL_INITIAL_AUDIO: True,
            cs.CALL_INITIAL_VIDEO: False
            }, dbus_interface=cs.CONN_IFACE_REQUESTS)

    ret = q.expect_many(EventPattern('dbus-signal', signal='NewChannels',
            predicate=lambda e:
                cs.CHANNEL_TYPE_CALL in e.args[0][0][1].values()),
        # a codec offer appears already!
        EventPattern('dbus-signal', signal='NewCodecOffer'))

    # all the basic stuff is already tested in call-basics.py
    chan = bus.get_object(conn.bus_name, ret[0].args[0][0][0])

    # there is no remote codec information, so this should be empty
    assertEquals(ret[1].args[2], [])

    # get a list of audio codecs we can support
    codecs = jt2.get_call_audio_codecs_dbus()

    # make sure UpdateCodecs fails
    props = chan.GetAll(cs.CHANNEL_TYPE_CALL,
            dbus_interface=dbus.PROPERTIES_IFACE)

    content = bus.get_object(conn.bus_name, props["Contents"][0])

    try:
        content.UpdateCodecs(codecs, dbus_interface=cs.CALL_CONTENT_IFACE_MEDIA)
    except DBusException, e:
        # this should fail now that there is a codec offer around
        if e.get_dbus_name() != cs.NOT_AVAILABLE:
            raise e
    else:
        assert false

    # we'll need these later
    content_props = content.GetAll(cs.CALL_CONTENT,
            dbus_interface=dbus.PROPERTIES_IFACE)

    # make an offer they can't refuse
    offer = bus.get_object(conn.bus_name, ret[1].args[1])
    props = offer.GetAll(cs.CALL_CONTENT_CODECOFFER,
            dbus_interface=dbus.PROPERTIES_IFACE)

    # this also needs to be empty
    assertEquals(props["RemoteContactCodecs"], [])

    offer.Accept(codecs, dbus_interface=cs.CALL_CONTENT_CODECOFFER)

    q.expect('dbus-signal', signal='CodecsChanged',
             args=[{self_handle: codecs}, []])

    chan.Accept(dbus_interface=cs.CHANNEL_TYPE_CALL)

    # add candidates
    cstream = bus.get_object(conn.bus_name, content_props["Streams"][0])
    candidates = jt2.get_call_remote_transports_dbus ()
    cstream.AddCandidates (candidates,
        dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    ret = q.expect_many(EventPattern('dbus-signal', signal='LocalCandidatesAdded'),
                        EventPattern('stream-iq',
            predicate=jp.action_predicate('session-initiate')))
    assertEquals (candidates, ret[0].args[0])

    jt2.parse_session_initiate(ret[1].query)

    cstream.CandidatesPrepared (dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    local_candidates = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA,
        "LocalCandidates", dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (candidates,  local_candidates)

    endpoints = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA,
        "Endpoints", dbus_interface=dbus.PROPERTIES_IFACE)
    assertLength (1, endpoints)

    # accept with a subset of the codecs
    old_codecs = jt2.audio_codecs
    jt2.audio_codecs = jt2.audio_codecs[:-1] # all but the last one

    # session-accept
    jt2.accept()

    ret = q.expect('dbus-signal', signal='NewCodecOffer')

    # make sure the codec offer has the updated codecs
    assertEquals(ret.args[2], codecs[:-1])

    # accept new offer
    offer = bus.get_object(conn.bus_name, ret.args[1])
    offer.Accept(codecs[:-1], dbus_interface=cs.CALL_CONTENT_CODECOFFER)

    # now we should both have the smaller set of codecs, easy
    o = q.expect ('dbus-signal', signal='CodecsChanged')
    assertEquals ([{ self_handle: codecs[:-1], remote_handle: codecs[:-1]}, []],
        o.args)

    chan.Close(dbus_interface=cs.CHANNEL)
    signal = q.expect('dbus-signal', signal='ChannelClosed')

    try_to_access_old_offer(conn, ret.args[0])

if __name__ == '__main__':
    test_dialects(test_incoming, [JingleProtocol031])
    test_dialects(test_outgoing, [JingleProtocol031])
