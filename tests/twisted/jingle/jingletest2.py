# New API for making it easier to write Jingle tests. The idea
# is not so much to hide away the details (this makes tests
# unreadable), but to make the expressions denser and more concise.
# Helper classes support different dialects so the test can
# be invoked for different (possibly all) dialects.
#
# This can be used in parallel with the old API, but should
# obsolete it in time.

from twisted.words.xish import domish, xpath
import random
from gabbletest import sync_stream, exec_test
from servicetest import EventPattern
import dbus
import ns
import os
import constants as cs

class JingleProtocol:
    """
    Defines a simple DSL for constructing Jingle messages.
    """

    def __init__(self, dialect):
        self.dialect = dialect 
        self.id_seq = 0

    def _simple_xml(self, node):
        "Construct domish.Element tree from tree of tuples"
        name, namespace, attribs, children = node
        el = domish.Element((namespace, name))
        for key, val in attribs.items():
            el[key] = val
        for c in children:
            if isinstance(c, tuple):
                el.addChild(self._simple_xml(c))
            elif isinstance(c, unicode):
                el.addContent(c)
            else:
                raise ValueError("invalid child object %r of type %r" % (c, type(c)))
        return el

    def xml(self, node):
        "Returns XML from tree of tuples"
        return self._simple_xml(node).toXml()

    def Iq(self, type, id, frm, to, children):
        "Creates an IQ element"
        if not id:
            id = 'seq%d' % self.id_seq
            self.id_seq += 1

        return ('iq', 'jabber:client',
            { 'type': type, 'from': frm, 'to': to, 'id': id },
            children)

    def SetIq(self, frm, to, children):
        "Creates a set IQ element"
        return self.Iq('set', None, frm, to, children)

    def ResultIq(self, to, iq, children):
        "Creates a result IQ element"
        return self.Iq('result', iq['id'], iq['to'], to,
            children)

    def ErrorIq(self, iq, errtype, errchild):
        "Creates an error IQ element, and includes the original stanza"
        return self.Iq('error', iq['id'], iq['to'], iq['from'],
            [ iq.firstChildElement(),
                ('error', None, { 'type': errtype, 'xmlns': ns.STANZA, },
                    [ errchild ]) ])

    def PayloadType(self, name, rate, id, parameters={}, **kw):
        "Creates a <payload-type> element"
        kw['name'] = name
        kw['rate'] = rate
        kw['id'] = id
        children = [self.Parameter(name, value)
                    for name, value in parameters.iteritems()]
        return ('payload-type', None, kw, children)

    def Parameter(self, name, value):
        "Creates a <parameter> element"
        return ('parameter', None, {'name': name, 'value': value}, [])

    def TransportGoogleP2P(self):
        "Creates a <transport> element for Google P2P transport"
        return ('transport', ns.GOOGLE_P2P, {}, [])

    def TransportIceUdp(self, remote_transports=[]):
        """
        Creates a <transport> element for ICE-UDP transport.
        If remote_transports is present, and of the form
        [(host, port, proto, subtype, profile, pref, transtype, user, pwd)]
        (basically a list of Media_Stream_Handler_Transport without the
        component number) then it will be converted to xml and added.
        """
        candidates = []
        attrs = {}
        for (host, port, proto, subtype, profile, pref, transtype, user, pwd
                ) in remote_transports:
            if "ufrag" not in attrs:
                attrs = {"ufrag": user, "pwd": pwd}
            else:
                assert (user == attrs["ufrag"] and pwd == attrs["pwd"]
                    ), "user and pwd should be the same across all candidates."

            node = ("candidate", None, {
                # ICE-CORE says it can be arbitrary string, even though XEP
                # gives an int as an example.
                "foundation": "fake",
                "ip": host,
                "port": str(port),
                "protocol": ["udp", "tcp"][proto],
                # Gabble multiplies by 65536 so we should too.
                "priority": str(int(pref * 65536)),
                "type":  ["host", "srflx", "srflx"][transtype],
                "network": "0",
                "generation": "0",# Increment this yourself if you care.
                "component": "1", # 1 is rtp, 2 is rtcp
                }, []) #NOTE: subtype and profile are unused
            candidates.append(node)
        return ('transport', ns.JINGLE_TRANSPORT_ICEUDP, attrs, candidates)

    def Presence(self, frm, to, caps):
        "Creates <presence> stanza with specified capabilities"
        children = []
        if caps:
            children = [ ('c', ns.CAPS, caps, []) ]
        return ('presence', 'jabber:client', { 'from': frm, 'to': to },
            children)

    def Query(self, node, xmlns, children):
        "Creates <query> element"
        attrs = {}
        if node:
            attrs['node'] = node
        return ('query', xmlns, attrs, children)

    def Feature(self, var):
        "Creates <feature> element"
        return ('feature', None, { 'var': var }, [])

    def action_predicate(self, action):
        def f(e):
            return self.match_jingle_action(e.query, action)

        return f

    def match_jingle_action(self, q, action):
        return q is not None and q.name == 'jingle' and q['action'] == action

    def _extract_session_id(self, query):
        return query['sid']

    def validate_session_initiate(self, query):
        raise NotImplementedError()

    def can_do_video(self):
        return True

    def can_do_video_only(self):
        return True

    def seperate_contents(self):
        return True

    def has_mutable_streams(self):
        return True

    def is_modern_jingle(self):
        return False

    def rtp_info_event(self, name):
        return None

    def rtp_info_event_list(self, name):
        e = self.rtp_info_event(name)
        return [e] if e is not None else []


class GtalkProtocol03(JingleProtocol):
    features = [ ns.GOOGLE_FEAT_VOICE, ns.GOOGLE_FEAT_VIDEO ]

    def __init__(self):
        JingleProtocol.__init__(self, 'gtalk-v0.3')

    def _action_map(self, action):
        map = {
            'session-initiate': 'initiate',
            'session-terminate': 'terminate',
            'session-accept': 'accept',
            'transport-info': 'candidates'
        }

        if action in map:
            return map[action]
        else:
            return action

    def Jingle(self, sid, initiator, action, children):
        action = self._action_map(action)
        return ('session', ns.GOOGLE_SESSION,
            { 'type': action, 'initiator': initiator, 'id': sid }, children)

    def PayloadType(self, name, rate, id, parameters={}, **kw):
        p = JingleProtocol.PayloadType(self, name, rate, id, parameters,
            **kw)
        if "type" in kw:
            namespaces = { "audio": ns.GOOGLE_SESSION_PHONE,
                           "video": ns.GOOGLE_SESSION_VIDEO,
            }
            p = p[:1] + (namespaces[kw["type"]],) + p[2:]

        return p

    # Gtalk has only one content, and <content> node is implicit
    def Content(self, name, creator, senders, children):
        # Normally <content> has <description> and <transport>, but we only
        # use <description>
        assert len(children) == 2
        return children[0]

    def Description(self, type, children):
        if type == 'audio':
            namespace = ns.GOOGLE_SESSION_PHONE
        elif type == 'video':
            namespace = ns.GOOGLE_SESSION_VIDEO
        else:
            namespace = 'unexistent-namespace'
        return ('description', namespace, {}, children)

    def match_jingle_action(self, q, action):
        action = self._action_map(action)
        return q is not None and q.name == 'session' and q['type'] == action

    # Content will never pick up transport, so this can return invalid value
    def TransportGoogleP2P(self):
        return None

    def _extract_session_id(self, query):
        return query['id']

    def can_do_video_only(self):
        return False

    def validate_session_initiate(self, query):
        sid = self._extract_session_id(query)

        # No transport in GTalk03
        assert xpath.queryForNodes('/session/transport', query) == None

        # Exactly one description in Gtalk03
        descs = xpath.queryForNodes('/session/description', query)
        assert len(descs) == 1

        desc = descs[0]

        # the ds is either audio or video
        assert desc.uri in [ ns.GOOGLE_SESSION_PHONE, ns.GOOGLE_SESSION_VIDEO ]

        if desc.uri == ns.GOOGLE_SESSION_VIDEO:
            # If it's a video call there should be some audio codecs as well
            assert xpath.queryForNodes(
                '/session/description/payload-type[@xmlns="%s"]' %
                    ns.GOOGLE_SESSION_PHONE, query)
            return (sid, ['fake-audio'], ['fake-video'])
        else:
            return (sid, ['fake-audio'], [])

    def seperate_contents(self):
        return False

    def has_mutable_streams(self):
        return False

class GtalkProtocol04(JingleProtocol):
    features = [ ns.GOOGLE_FEAT_VOICE, ns.GOOGLE_P2P ]

    def __init__(self):
        JingleProtocol.__init__(self, 'gtalk-v0.4')

    def _action_map(self, action):
        map = {
            'session-initiate': 'initiate',
            'session-terminate': 'terminate',
            'session-accept': 'accept',
        }

        if action in map:
            return map[action]
        else:
            return action

    def Jingle(self, sid, initiator, action, children):
        # ignore Content and go straight for its children
        if len(children) == 1 and children[0][0] == 'dummy-content':
            children = [ children[0][3][0], children[0][3][1] ]

        action = self._action_map(action)
        return ('session', ns.GOOGLE_SESSION,
            { 'type': action, 'initiator': initiator, 'id': sid }, children)

    # hacky: parent Jingle node should just pick up our children
    def Content(self, name, creator, senders, children):
        return ('dummy-content', None, {}, children)

    def Description(self, type, children):
        return ('description', ns.GOOGLE_SESSION_PHONE, {}, children)

    def match_jingle_action(self, q, action):
        action = self._action_map(action)
        return q is not None and q.name == 'session' and q['type'] == action

    def _extract_session_id(self, query):
        return query['id']

    def validate_session_initiate(self, query):
        # FIXME: validate it!
        return (self._extract_session_id(query), ['fake-audio'], [])

    def can_do_video(self):
        return False

class JingleProtocol015(JingleProtocol):
    features = [ ns.GOOGLE_P2P, ns.JINGLE_015, ns.JINGLE_015_AUDIO,
        ns.JINGLE_015_VIDEO ]

    def __init__(self):
        JingleProtocol.__init__(self, 'jingle-v0.15')

    def Jingle(self, sid, initiator, action, children):
        return ('jingle', ns.JINGLE_015,
            { 'action': action, 'initiator': initiator, 'sid': sid }, children)

    # Note: senders weren't mandatory in this dialect
    def Content(self, name, creator, senders, children):
        attribs = { 'name': name, 'creator': creator }
        if senders:
            attribs['senders'] = senders
        return ('content', None, attribs, children)

    def Description(self, type, children):
        if type == 'audio':
            namespace = ns.JINGLE_015_AUDIO
        elif type == 'video':
            namespace = ns.JINGLE_015_VIDEO
        else:
            namespace = 'unexistent-namespace'
        return ('description', namespace, { 'type': type }, children)

    def validate_session_initiate(self, query):
        contents = xpath.queryForNodes(
            '/jingle[@xmlns="%s"]/content' % ns.JINGLE_015,
            query)

        audio, video = [], []

        for c in contents:
            a_desc = xpath.queryForNodes(
                '/content/description[@xmlns="%s"]' % ns.JINGLE_015_AUDIO,
                c)
            v_desc = xpath.queryForNodes(
                '/content/description[@xmlns="%s"]' % ns.JINGLE_015_VIDEO,
                c)

            if a_desc is not None:
                assert len(a_desc) == 1, c.toXml()
                assert v_desc is None
                audio.append(c['name'])
            elif v_desc is not None:
                assert len(v_desc) == 1, c.toXml()
                assert a_desc is None
                video.append(c['name'])
            else:
                assert False, c.toXml()

        assert len(audio) + len(video) > 0, query.toXml()

        return (self._extract_session_id(query), audio, video)

class JingleProtocol031(JingleProtocol):
    features = [ ns.JINGLE, ns.JINGLE_RTP, ns.JINGLE_RTP_AUDIO,
        ns.JINGLE_RTP_VIDEO, ns.GOOGLE_P2P ]

    def __init__(self):
        JingleProtocol.__init__(self, 'jingle-v0.31')

    def Jingle(self, sid, initiator, action, children):
        return ('jingle', ns.JINGLE,
            { 'action': action, 'initiator': initiator, 'sid': sid }, children)

    def Content(self, name, creator, senders, children):
        if not senders:
            senders = 'both'
        return ('content', None,
            { 'name': name, 'creator': creator, 'senders': senders }, children)

    def Description(self, type, children):
        return ('description', ns.JINGLE_RTP, { 'media': type }, children)

    def is_modern_jingle(self):
        return True

    def rtp_info_event(self, name):
        def p(e):
            query = e.query
            if not self.match_jingle_action(query, 'session-info'):
                return False
            n = query.firstChildElement()
            return n is not None and n.uri == ns.JINGLE_RTP_INFO_1 and \
                n.name == name

        return EventPattern('stream-iq', predicate=p)

    def validate_session_initiate(self, query):
        contents = xpath.queryForNodes(
            '/jingle[@xmlns="%s"]/content' % ns.JINGLE,
            query)

        audio, video = [], []

        for c in contents:
            descs = xpath.queryForNodes(
                '/content/description[@xmlns="%s"]' % ns.JINGLE_RTP,
                c)

            assert len(descs) == 1, c.toXml()

            d = descs[0]

            if d['media'] == 'audio':
                audio.append(c['name'])
            elif d['media'] == 'video':
                video.append(c['name'])
            else:
                assert False, c.toXml()

        assert len(audio) + len(video) > 0, query.toXml()

        return (self._extract_session_id(query), audio, video)

class JingleTest2:
    # Default caps for the remote end
    remote_caps = { 'ext': '', 'ver': '0.0.0',
             'node': 'http://example.com/fake-client0' }

    # Default audio codecs for the remote end
    audio_codecs = [ ('GSM', 3, 8000), ('PCMA', 8, 8000), ('PCMU', 0, 8000) ]

    # Default video codecs for the remote end. I have no idea what's
    # a suitable value here...
    video_codecs = [ ('WTF', 42, 80000) ]

    # Default candidates for the remote end
    remote_transports = [
          ( "192.168.0.1", # host
            666, # port
            0, # protocol = TP_MEDIA_STREAM_BASE_PROTO_UDP
            "RTP", # protocol subtype
            "AVP", # profile
            1.0, # preference
            0, # transport type = TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL,
            "username",
            "password" ) ]



    def __init__(self, jp, conn, q, stream, jid, peer):
        self.jp = jp
        self.conn = conn
        self.q = q
        self.jid = jid
        self.peer = peer
        self.stream = stream
        self.sid = 'sess' + str(int(random.random() * 10000))

    def prepare(self, send_presence=True):
        # If we need to override remote caps, feats, codecs or caps,
        # we should do it prior to calling this method.

        # Connecting
        self.conn.Connect()

        # Catch events: status connecting, authentication, our presence update,
        # status connected, vCard query
        # If we don't catch the vCard query here, it can trip us up later:
        # http://bugs.freedesktop.org/show_bug.cgi?id=19161
        self.q.expect_many(
                EventPattern('dbus-signal', signal='StatusChanged',
                    args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED]),
                EventPattern('stream-authenticated'),
                EventPattern('dbus-signal', signal='PresenceUpdate',
                    args=[{1L: (0L, {u'available': {}})}]),
                EventPattern('dbus-signal', signal='StatusChanged',
                    args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
                EventPattern('stream-iq', to=None, query_ns='vcard-temp',
                    query_name='vCard'),
                )

        if send_presence:
            self.send_presence_and_caps()

    def send_presence_and_caps(self):
        # We need remote end's presence for capabilities
        self.stream.send(self.jp.xml(
            self.jp.Presence(self.peer, self.jid, self.remote_caps)))

        # Gabble doesn't trust it, so makes a disco
        event = self.q.expect('stream-iq', query_ns=ns.DISCO_INFO, to=self.peer)

        # jt.send_remote_disco_reply(event.stanza)
        self.stream.send(self.jp.xml(self.jp.ResultIq(self.jid, event.stanza,
            [ self.jp.Query(None, ns.DISCO_INFO,
                [ self.jp.Feature(x) for x in self.jp.features ]) ]) ))

        # Force Gabble to process the caps before doing any more Jingling
        sync_stream(self.q, self.stream)

    def generate_contents(self):
        assert len(self.audio_names + self.video_names) > 0

        jp = self.jp

        assert len(self.video_names) == 0 or jp.can_do_video()

        contents = []

        if not jp.seperate_contents() and self.video_names:
            assert jp.can_do_video()
            assert self.audio_names

            contents.append(
                jp.Content('stream0', 'initiator', 'both', [
                    jp.Description('video', [
                        jp.PayloadType(name, str(rate), str(id)) for
                            (name, id, rate) in self.video_codecs ] +
                        [ jp.PayloadType(name, str(rate), str(id),
                            {}, type = "audio" ) for (name, id, rate, )
                                in self.audio_codecs ],
                        ),
                    jp.TransportGoogleP2P() ])
             )
        else:
            def mk_content(name, media, codecs):
                contents.append(
                    jp.Content(name, 'initiator', 'both', [
                        jp.Description(media, [
                            jp.PayloadType(name, str(rate), str(id)) for
                            (name, id, rate) in codecs ]),
                    jp.TransportGoogleP2P() ])
                )

            for name in self.audio_names:
                mk_content(name, 'audio', self.audio_codecs)

            for name in self.video_names:
                mk_content(name, 'video', self.video_codecs)

        return contents

    def incoming_call(self, audio=True, video=False):
        jp = self.jp

        self.audio_names = ['audio1'] if audio else []
        self.video_names = ['video1'] if video else []

        contents = self.generate_contents()

        node = jp.SetIq(self.peer, self.jid, [
            jp.Jingle(self.sid, self.peer, 'session-initiate', contents),
            ])
        self.stream.send(jp.xml(node))

    def parse_session_initiate (self, query):
        # Validate the session initiate and get some useful info from it
        self.sid, self.audio_names, self.video_names = \
            self.jp.validate_session_initiate(query)

    def accept(self):
        jp = self.jp

        contents = self.generate_contents()
        node = jp.SetIq(self.peer, self.jid, [
            jp.Jingle(self.sid, self.peer, 'session-accept',
                contents) ])
        self.stream.send(jp.xml(node))

    def content_accept(self, query, media):
        """
        Accepts a content-add stanza containing a single <content> of the given
        media type.
        """
        jp = self.jp
        assert jp.seperate_contents()
        c = query.firstChildElement()

        if media == 'audio':
            codecs = self.audio_codecs
        elif media == 'video':
            codecs = self.video_codecs
        else:
            assert False

        # Remote end finally accepts
        node = jp.SetIq(self.peer, self.jid, [
            jp.Jingle(self.sid, self.peer, 'content-accept', [
                jp.Content(c['name'], c['creator'], c['senders'], [
                    jp.Description(media, [
                        jp.PayloadType(name, str(rate), str(id)) for
                            (name, id, rate) in codecs ]),
                jp.TransportGoogleP2P() ]) ]) ])
        self.stream.send(jp.xml(node))

    def terminate(self, reason=None, text=""):
        jp = self.jp

        if reason is not None and jp.is_modern_jingle():
            body = [("reason", None, {},
                        [(reason, None, {}, []),
                         ("text", None, {}, [text]),
                        ]
                    )]
        else:
            body = []

        iq = jp.SetIq(self.peer, self.jid, [
            jp.Jingle(self.sid, self.peer, 'session-terminate', body) ])
        self.stream.send(jp.xml(iq))

    def dbusify_codecs(self, codecs):
        dbussed_codecs = [ (id, name, 0, rate, 0, {} )
                            for (name, id, rate) in codecs ]
        return dbus.Array(dbussed_codecs, signature='(usuuua{ss})')

    def dbusify_codecs_with_params(self, codecs):
        dbussed_codecs = [ (id, name, 0, rate, 0, params)
                            for (name, id, rate, params) in codecs ]
        return dbus.Array(dbussed_codecs, signature='(usuuua{ss})')

    def get_audio_codecs_dbus(self):
        return self.dbusify_codecs(self.audio_codecs)

    def get_video_codecs_dbus(self):
        return self.dbusify_codecs(self.video_codecs)

    def get_remote_transports_dbus(self):
        return dbus.Array([
            (dbus.UInt32(1 + i), host, port, proto, subtype,
                profile, pref, transtype, user, pwd)
                for i, (host, port, proto, subtype, profile,
                    pref, transtype, user, pwd)
                in enumerate(self.remote_transports) ],
            signature='(usuussduss)')


def test_dialects(f, dialects):
    for dialect in dialects:
        exec_test(
            lambda q, bus, conn, stream: f(dialect(), q, bus, conn, stream))

def test_all_dialects(f):
    dialectmap = { "jingle015": JingleProtocol015,
        "jingle031": JingleProtocol031,
        "gtalk03": GtalkProtocol03,
        "gtalk04":  GtalkProtocol04
    }
    dialects = []

    jd = os.getenv("JINGLE_DIALECTS")
    if jd == None:
        dialects = dialectmap.values()
    else:
        for d in jd.split (','):
            dialects.append(dialectmap[d])
    test_dialects(f,  dialects)
