"""
Tests workarounds for calls with the GMail client, which supports a (currently
quirky) variation on the theme of modern Jingle.
"""

from servicetest import EventPattern, wrap_channel, make_channel_proxy, assertEquals
from gabbletest import elem, elem_iq, exec_test
from jingletest2 import JingleTest2, JingleProtocol031
import ns
import constants as cs
from twisted.words.xish import xpath

from config import VOIP_ENABLED

if not VOIP_ENABLED:
    print "NOTE: built with --disable-voip"
    raise SystemExit(77)

class GMail(JingleTest2):
    remote_caps = {
        'ext': 'pmuc-v1 sms-v1 camera-v1 video-v1 voice-v1',
        'ver': '1.1',
        'node': 'http://mail.google.com/xmpp/client/caps',
    }

def test(q, bus, conn, stream):
    peer = 'foo@gmail.com/gmail.7E1F07D0'
    self = 'test@localhost/test'
    jp = JingleProtocol031()
    jt = GMail(jp, conn, q, stream, 'test@localhost', peer)
    jt.prepare(send_roster=False)

    sid = 'c1025763497'
    si = elem_iq(stream, 'set', from_=peer, to=self)(
      elem(ns.JINGLE, 'jingle', action='session-initiate', sid=sid, initiator=peer)(
        elem('content', name='video')(
          elem(ns.JINGLE_RTP, 'description', media='video')(
            elem('payload-type', id='99', name='H264-SVC')(
              elem('parameter', name='width', value='320'),
              elem('parameter', name='height', value='200'),
              elem('parameter', name='framerate', value='30'),
            ),
            # ... other codecs elided ...
            elem('encryption'),
          ),
          elem(ns.GOOGLE_P2P, 'transport'),
        ),
        elem('content', name='audio')(
          elem(ns.JINGLE_RTP, 'description', media='audio')(
            elem('payload-type', id='103', name='ISAC', clockrate='16000')(
              elem('parameter', name='bitrate', value='32000'),
            ),
            # ... other codecs elided ...
            elem('encryption'),
          ),
          elem(ns.GOOGLE_P2P, 'transport'),
        )
      ),
      elem(ns.GOOGLE_SESSION, 'session', action='initiate', sid='c1025763497', initiator=peer)(
        elem(ns.GOOGLE_SESSION_VIDEO, 'description')(
          elem('payload-type', id='99', name='H264-SVC', width='320', height='200', framerate='30'),
          # ... other codecs elided ...
          elem(ns.JINGLE_RTP, 'encryption')(
            elem(ns.GOOGLE_SESSION_VIDEO, 'usage'),
          ),
          elem(ns.GOOGLE_SESSION_PHONE, 'payload-type', id='103', name='ISAC', bitrate='32000', clockrate='16000'),
          # ... other codecs elided ...
          elem(ns.JINGLE_RTP, 'encryption')(
            elem(ns.GOOGLE_SESSION_PHONE, 'usage'),
          ),
        ),
      ),
    )
    stream.send(si)

    nc, nsh = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannels'),
        EventPattern('dbus-signal', signal='NewSessionHandler'),
        )

    path, properties = nc.args[0][0]

    # It's an audio+video call
    assert properties[cs.INITIAL_AUDIO]
    assert properties[cs.INITIAL_VIDEO]
    # Google can't add and remove streams on the fly. We special-case GMail.
    assert properties[cs.IMMUTABLE_STREAMS]

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia')
    session_handler = make_channel_proxy(conn, nsh.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    path, _, _, _ = q.expect('dbus-signal', signal='NewStreamHandler').args
    stream1 = make_channel_proxy(conn, path, 'Media.StreamHandler')
    path, _, _, _ = q.expect('dbus-signal', signal='NewStreamHandler').args
    stream2 = make_channel_proxy(conn, path, 'Media.StreamHandler')

    stream1.Ready([])
    stream2.Ready([])

    # Audio rtcp
    stream.send(
      elem_iq(stream, from_=peer, to=self, type='set')(
        elem(ns.JINGLE, 'jingle', action='transport-info', sid=sid)(
          elem('content', name='audio')(
            elem(ns.GOOGLE_P2P, 'transport')(
              elem('candidate', address='172.22.64.192', port='54335',
                   name='rtcp', username='+wJqkmRVYotCz+Rd',
                   password='POWPzg5Pks4+ywAz', preference='1', protocol='udp',
                   generation='0', network='1', type='local')
            )
          )
        )
      )
    )
    q.expect('dbus-signal', signal='AddRemoteCandidate', path=stream1.object_path)

    # audio rtp
    stream.send(
      elem_iq(stream, from_=peer, to=self, type='set')(
        elem(ns.JINGLE, 'jingle', action='transport-info', sid=sid)(
          elem('content', name='audio')(
            elem(ns.GOOGLE_P2P, 'transport')(
              elem('candidate', address='172.22.64.192', port='54337',
                   name='rtp', username='F7rgdWcCgH3Q/HgE',
                   password='ioh2IDwd3iZEZHzM', preference='1', protocol='udp',
                   generation='0', network='1', type='local')
            )
          )
        )
      )
    )
    q.expect('dbus-signal', signal='AddRemoteCandidate', path=stream1.object_path)

    # video rtcp: note the weird name='' field which Gabble has to work around
    stream.send(
      elem_iq(stream, from_=peer, to=self, type='set')(
        elem(ns.JINGLE, 'jingle', action='transport-info', sid=sid)(
          elem('content', name='video')(
            elem(ns.GOOGLE_P2P, 'transport')(
              elem('candidate', address='172.22.64.192', port='54339',
                   name='video_rtcp', username='fnLduEIu6VHsSOqh',
                   password='IYeNu/HWzMpx2zrS', preference='1', protocol='udp',
                   generation='0', network='1', type='local')
            )
          )
        )
      )
    )
    q.expect('dbus-signal', signal='AddRemoteCandidate', path=stream2.object_path)

    # video rtp: ditto
    stream.send(
      elem_iq(stream, from_=peer, to=self, type='set')(
        elem(ns.JINGLE, 'jingle', action='transport-info', sid=sid)(
          elem('content', name='video')(
            elem(ns.GOOGLE_P2P, 'transport')(
              elem('candidate', address='172.22.64.192', port='54341',
                   name='video_rtp', username='mZVBFdQ2LyAP6oyE',
                   password='3uoyCHP8zwE+/Ylw', preference='1', protocol='udp',
                   generation='0', network='1', type='local')
            )
          )
        )
      )
    )
    q.expect('dbus-signal', signal='AddRemoteCandidate', path=stream2.object_path)

    # Test that we're sending with name='video_rtp' as well, but only for the video stream.
    stream1.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    e = q.expect('stream-iq', predicate=jp.action_predicate('transport-info'))
    candidate = xpath.queryForNodes(
        '/iq/jingle/content[@name="audio"]/transport/candidate',
        e.stanza)[0]
    assertEquals('rtp', candidate['name'])

    stream2.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    e = q.expect('stream-iq', predicate=jp.action_predicate('transport-info'))
    candidate = xpath.queryForNodes(
        '/iq/jingle/content[@name="video"]/transport/candidate',
        e.stanza)[0]
    assertEquals('video_rtp', candidate['name'])

if __name__ == '__main__':
    exec_test(test)
