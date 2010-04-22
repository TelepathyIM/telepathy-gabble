"""
Testing different methods related to the CodecOffer interface.
"""

import dbus

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
    [path, codecmap] = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "CodecOffer", dbus_interface=dbus.PROPERTIES_IFACE)

    assertNotEquals ("/", path)

    offer = bus.get_object (conn.bus_name, path)
    codecmap_property = offer.Get (cs.CALL_CONTENT_CODECOFFER,
        "RemoteContactCodecMap", dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (codecmap, codecmap_property)

def accept_offer (q, bus, conn, self_handle, remote_handle,
        content, codecs, offer_path = None):
    [path, codecmap] = content.Get (cs.CALL_CONTENT_IFACE_MEDIA,
                "CodecOffer", dbus_interface=dbus.PROPERTIES_IFACE)

    offer = bus.get_object (conn.bus_name, path)

    offer.Accept (codecs, dbus_interface=cs.CALL_CONTENT_CODECOFFER)

    current_codecs = content.Get (cs.CALL_CONTENT_IFACE_MEDIA,
                "ContactCodecMap", dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (codecs,  current_codecs[self_handle])

    o = q.expect ('dbus-signal', signal='CodecsChanged')

    assertEquals ([{ self_handle: codecs, remote_handle: codecs}, []],
        o.args)

def reject_offer (q, bus, conn,
        content, codecs, offer_path = None):
    [path, codecmap] = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "CodecOffer", dbus_interface=dbus.PROPERTIES_IFACE)

    offer = bus.get_object (conn.bus_name, path)

    offer.Reject (dbus_interface=cs.CALL_CONTENT_CODECOFFER)

def update_codecs(jt2):
    contents = jt2.generate_contents()

    node = jt2.jp.SetIq(jt2.peer, jt2.jid, [
        jt2.jp.Jingle(jt2.sid, jt2.peer, 'description-info', contents),
        ])
    jt2.stream.send(jt2.jp.xml(node))

def test(jp, q, bus, conn, stream):
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

    update_codecs(jt2)
    signal = q.expect('dbus-signal', signal='NewCodecOffer')
    check_offer(bus, conn, content)
    reject_offer(q, bus, conn, content, codecs)

    update_codecs(jt2)
    signal = q.expect('dbus-signal', signal='NewCodecOffer')
    check_offer(bus, conn, content)
    accept_offer(q, bus, conn, self_handle, remote_handle,
        content, codecs)

    update_codecs(jt2)
    signal = q.expect('dbus-signal', signal='NewCodecOffer')
    check_offer(bus, conn, content)

    [path, codecmap] = content.Get (cs.CALL_CONTENT_IFACE_MEDIA,
                "CodecOffer", dbus_interface=dbus.PROPERTIES_IFACE)

    chan.Close(dbus_interface=cs.CHANNEL)
    signal = q.expect('dbus-signal', signal='ChannelClosed')

    try:
        offer = bus.get_object (conn.bus_name, path)
        ret = offer.GetAll (cs.CALL_CONTENT_CODECOFFER,
            dbus_interface=dbus.PROPERTIES_IFACE)
    except Exception, e:
        pass
    else:
        assert False, 'Offer still exists'

    try:
        ret = conn.GetAll (cs.CONN, dbus_interface=dbus.PROPERTIES_IFACE)
    except Exception, e:
        print 'Gabble probably crashed'
        raise e
    else:
        # depending on the age of our telepathy-glib, we have at least
        # SelfHandle, and might also have Interfaces and Status
        assert len(ret) > 0

if __name__ == '__main__':
    test_dialects(test, [JingleProtocol031])
