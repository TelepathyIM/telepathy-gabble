"""
Test receiving another contact's capabilities.
"""

import dbus

from servicetest import EventPattern
from gabbletest import exec_test, make_result_iq, make_presence
import constants as cs

icaps_attr  = cs.CONN_IFACE_CAPS + "/caps"
basic_caps = [(2, cs.CHANNEL_TYPE_TEXT, 3, 0)]

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    presence = make_presence('bob@foo.com/Foo', status='hello')
    stream.send(presence)

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
           args=[{2L: (0L, {u'available': {'message': 'hello'}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
           args=[{2L: (2, u'available', 'hello')}]))

    # no special capabilities
    assert conn.Capabilities.GetCapabilities([2]) == basic_caps
    assert conn.Contacts.GetContactAttributes(
        [2], [cs.CONN_IFACE_CAPS], False) == \
        { 2L: { icaps_attr: basic_caps,
                cs.CONN + '/contact-id': 'bob@foo.com'}}

    # send updated presence with Jingle audio/video caps info. we turn on both
    # audio and video at the same time to test that all of the capabilities are
    # discovered before any capabilities change signal is emitted
    presence = make_presence('bob@foo.com/Foo', status='hello',
        caps={
            'node': 'http://telepathy.freedesktop.org/fake-client',
            'ver' : '0.1',
            'ext' : 'video',
        })
    stream.send(presence)

    # Gabble looks up both the version and the video bundles, in any order
    (version_event, video_event) = q.expect_many(
        EventPattern('stream-iq', to='bob@foo.com/Foo',
            query_ns='http://jabber.org/protocol/disco#info',
            query_node='http://telepathy.freedesktop.org/fake-client#0.1'),
        EventPattern('stream-iq', to='bob@foo.com/Foo',
            query_ns='http://jabber.org/protocol/disco#info',
            query_node='http://telepathy.freedesktop.org/fake-client#video'))

    # reply to the video bundle query first - this capability alone is not
    # sufficient to make us callable
    result = make_result_iq(stream, video_event.stanza)
    query = result.firstChildElement()
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle/description/video'
    stream.send(result)

    # reply to the version bundle query, which should make us audio and
    # video callable
    result = make_result_iq(stream, version_event.stanza)
    query = result.firstChildElement()
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle'
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle/description/audio'
    feature = query.addElement('feature')
    feature['var'] = 'http://www.google.com/transport/p2p'
    stream.send(result)

    # we can now do audio and video calls
    event = q.expect('dbus-signal', signal='CapabilitiesChanged',
        args=[[(2, cs.CHANNEL_TYPE_STREAMED_MEDIA, 0, 3,
            0, cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_VIDEO)]])

    caps = conn.Contacts.GetContactAttributes([2], [cs.CONN_IFACE_CAPS], False)
    assert caps.keys() == [2L]
    assert icaps_attr in caps[2L]
    assert len(caps[2L][icaps_attr]) == 2
    assert basic_caps[0] in caps[2L][icaps_attr]
    assert (2, cs.CHANNEL_TYPE_STREAMED_MEDIA, 3, 3) in caps[2L][icaps_attr]

    # send updated presence without video support
    presence = make_presence('bob@foo.com/Foo', status='hello',
        caps={
            'node': 'http://telepathy.freedesktop.org/fake-client',
            'ver' : '0.1',
        })
    stream.send(presence)

    # we can now do only audio calls (and as a result have the ImmutableStreams
    # cap)
    event = q.expect('dbus-signal', signal='CapabilitiesChanged',
        args=[[(2, cs.CHANNEL_TYPE_STREAMED_MEDIA, 3, 3,
            cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_VIDEO,
            cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_IMMUTABLE_STREAMS)]])

    caps = conn.Contacts.GetContactAttributes([2], [cs.CONN_IFACE_CAPS], False)
    assert caps.keys() == [2L]
    assert icaps_attr in caps[2L]
    assert len(caps[2L][icaps_attr]) == 2
    assert basic_caps[0] in caps[2L][icaps_attr]
    assert (2, cs.CHANNEL_TYPE_STREAMED_MEDIA, 3,
        cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_IMMUTABLE_STREAMS) \
        in caps[2L][icaps_attr]

    # go offline
    presence = make_presence('bob@foo.com/Foo', type='unavailable')
    stream.send(presence)

    # can't do audio calls any more
    event = q.expect('dbus-signal', signal='CapabilitiesChanged',
        args=[[(2, cs.CHANNEL_TYPE_STREAMED_MEDIA, 3, 0,
            cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_IMMUTABLE_STREAMS,
            0)]])

    # Contact went offline and the handle is now invalid
    assert conn.Contacts.GetContactAttributes(
        [2], [cs.CONN_IFACE_CAPS], False) == {}

    # regression test for fd.o #15198: getting caps of invalid handle crashed
    try:
        conn.Capabilities.GetCapabilities([31337])
    except dbus.DBusException, e:
        pass
    else:
        assert False, "Should have had an error!"

if __name__ == '__main__':
    exec_test(test)

