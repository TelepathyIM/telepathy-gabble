
"""
Test capabilities.
"""

import dbus

from twisted.words.xish import domish

from servicetest import EventPattern
from gabbletest import exec_test, make_result_iq

text = 'org.freedesktop.Telepathy.Channel.Type.Text'
sm = 'org.freedesktop.Telepathy.Channel.Type.StreamedMedia'
basic_caps = [(2, text, 3, 0)]

def make_presence(from_jid, type, status):
    presence = domish.Element((None, 'presence'))

    if from_jid is not None:
        presence['from'] = from_jid

    if type is not None:
        presence['type'] = type

    if status is not None:
        presence.addElement('status', content=status)

    return presence

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    presence = make_presence('bob@foo.com/Foo', None, 'hello')
    stream.send(presence)

    _, _ = q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
           args=[{2L: (0L, {u'available': {'message': 'hello'}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
           args=[{2L: (2, u'available', 'hello')}]))

    # no special capabilities
    assert conn.Capabilities.GetCapabilities([2]) == basic_caps

    # send updated presence with Jingle audio/video caps info. we turn on both
    # audio and video at the same time to test that all of the capabilities are
    # discovered before any capabilities change signal is emitted
    presence = make_presence('bob@foo.com/Foo', None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = 'http://telepathy.freedesktop.org/fake-client'
    c['ver'] = '0.1'
    c['ext'] = 'video'
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
        args=[[(2, sm, 0, 3, 0, 3)]])

    # send updated presence without video support
    presence = make_presence('bob@foo.com/Foo', None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = 'http://telepathy.freedesktop.org/fake-client'
    c['ver'] = '0.1'
    stream.send(presence)

    # we can now do only audio calls
    event = q.expect('dbus-signal', signal='CapabilitiesChanged',
        args=[[(2, sm, 3, 3, 3, 1)]])

    # go offline
    presence = make_presence('bob@foo.com/Foo', 'unavailable', None)
    stream.send(presence)

    # can't do audio calls any more
    event = q.expect('dbus-signal', signal='CapabilitiesChanged',
        args=[[(2, sm, 3, 0, 1, 0)]])

    # regression test for fd.o #15198: getting caps of invalid handle crashed
    try:
        conn.Capabilities.GetCapabilities([31337])
    except dbus.DBusException, e:
        pass
    else:
        assert False, "Should have had an error!"

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

