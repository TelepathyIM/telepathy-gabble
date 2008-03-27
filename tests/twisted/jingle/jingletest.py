"""
Jingle (XEP-0166) testing support.
"""

import random
from gabbletest import make_result_iq
from twisted.words.xish import domish
from twisted.words.protocols.jabber.client import IQ
import dbus

def make_presence(fromjid, tojid, caps=None):
    el = domish.Element(('jabber:client', 'presence',))
    el['from'] = fromjid
    el['to'] = tojid

    if caps:
        cel = domish.Element(('http://jabber.org/protocol/caps', 'c'))
        for key,value in caps.items():
            cel[key] = value
        el.addChild(cel)

    return el


def make_caps_disco_reply(stream, req, features):
    iq = make_result_iq(stream, req)
    query = iq.firstChildElement()

    for f in features:
        el = domish.Element((None, 'feature'))
        el['var'] = f
        query.addChild(el)

    return iq


class JingleTest:

    def __init__(self, stream, local_jid, remote_jid):
        self.stream = stream
        self.local_jid = local_jid
        self.remote_jid = remote_jid
        self.session_id = 'sess' + str(int(random.random() * 10000))
        self.google_mode = False

        # Default caps for the remote end
        self.remote_caps = { 'ext': 'voice-v1 jingle-audio jingle-video', 'ver': '0.6.0',
                 'node': 'http://telepathy.freedesktop.org/caps' }

        # Default feats for remote end
        self.remote_feats = [ 'http://www.google.com/xmpp/protocol/session',
              'http://www.google.com/transport/p2p',
              'http://jabber.org/protocol/jingle' ]

        # Default audio codecs for the remote end
        self.audio_codecs = [ ('GSM', 3, 8000), ('PCMA', 8, 8000), ('PCMU', 0, 8000) ]

        # Default video codecs for the remote end. I have no idea what's
        # a suitable value here...
        self.video_codecs = [ ('WTF', 42, 80000) ]

        # Default candidates for the remote end
        self.remote_transports = [
              ( "192.168.0.1", # host
                666, # port
                0, # protocol = TP_MEDIA_STREAM_BASE_PROTO_UDP
                "RTP", # protocol subtype
                "AVP", # profile
                1.0, # preference
                0, # transport type = TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL,
                "username",
                "password" ) ]


    def get_video_codecs_dbus(self):
        return dbus.Array([ (id, name, 0, rate, 0, {} ) for (name, id, rate) in self.video_codecs ],
            signature='(usuuua{ss})')


    def get_audio_codecs_dbus(self):
        return dbus.Array([ (id, name, 0, rate, 0, {} ) for (name, id, rate) in self.audio_codecs ],
            signature='(usuuua{ss})')


    def get_remote_transports_dbus(self):
        return dbus.Array([
            (dbus.UInt32(i), host, port, proto, subtype,
                profile, pref, transtype, user, pwd)
                for i, (host, port, proto, subtype, profile,
                    pref, transtype, user, pwd)
                in enumerate(self.remote_transports) ],
            signature='(usuussduss)')


    def _jingle_stanza(self, action):
        iq = IQ(self.stream, 'set')
        iq['from'] = self.remote_jid
        iq['to'] = self.local_jid
        jingle = domish.Element(("http://jabber.org/protocol/jingle", 'jingle'))
        if self.direction == 'incoming':
            jingle['initiator'] = self.remote_jid
        elif self.direction == 'outgoing':
            jingle['initiator'] = self.local_jid

        jingle['action'] = action
        jingle['sid'] = self.session_id
        iq.addChild(jingle)
        return (iq, jingle)

    def _gtalk_stanza(self, action):
        iq = IQ(self.stream, 'set')
        iq['from'] = self.remote_jid
        iq['to'] = self.local_jid
        sess = domish.Element(("http://www.google.com/session", 'session'))
        if self.direction == 'incoming':
            sess['initiator'] = self.remote_jid
        elif self.direction == 'outgoing':
            sess['initiator'] = self.local_jid

        sess['type'] = action
        sess['id'] = self.session_id
        iq.addChild(sess)
        return (iq, sess)


    def send_remote_presence(self):
        presence = make_presence(self.remote_jid, self.local_jid,
            self.remote_caps)
        self.stream.send(presence.toXml())


    def send_remote_disco_reply(self, stanza):
        reply = make_caps_disco_reply(self.stream, stanza,
            self.remote_feats)
        self.stream.send(reply.toXml())


    def incoming_call(self):
        self.direction = 'incoming'

        if self.google_mode:
            iq, sess = self._gtalk_stanza('initiate')
            desc_ns = 'http://www.google.com/session/phone'
        else:
            iq, jingle = self._jingle_stanza('session-initiate')
            desc_ns = 'http://jabber.org/protocol/jingle/description/audio'

        content = domish.Element((None, 'content'))
        content['creator'] = 'initiator'
        content['name'] = 'audio1'

        desc = domish.Element((desc_ns, 'description'))
        for codec, id, rate in self.audio_codecs:
            p = domish.Element((None, 'payload-type'))
            p['name'] = codec
            p['id'] = str(id)
            if self.google_mode:
                p['clockrate'] = p['bitrate'] = str(rate)
            else:
                p['rate'] = str(rate)
            desc.addChild(p)

        xport = domish.Element(("http://www.google.com/transport/p2p", 'transport'))

        if self.google_mode:
            sess.addChild(desc)
            sess.addChild(xport)
        else:
            jingle.addChild(content)
            content.addChild(desc)
            content.addChild(xport)

        self.stream.send(iq.toXml())


    def outgoing_call_reply(self, session_id, accept):
        self.session_id = session_id
        self.direction = 'outgoing'

        if not accept:
            self.remote_terminate()
            return

        iq, jingle = self._jingle_stanza('session-accept')

        content = domish.Element((None, 'content'))
        content['creator'] = 'initiator'
        content['name'] = 'audio1'
        jingle.addChild(content)

        desc = domish.Element(("http://jabber.org/protocol/jingle/description/audio", 'description'))
        for codec, id, rate in self.audio_codecs:
            p = domish.Element((None, 'payload-type'))
            p['name'] = codec
            p['id'] = str(id)
            p['rate'] = str(rate)
            desc.addChild(p)

        content.addChild(desc)

        xport = domish.Element(("http://www.google.com/transport/p2p", 'transport'))
        content.addChild(xport)

        self.stream.send(iq.toXml())


    def remote_terminate(self):
        if self.google_mode:
            iq, _ = self._gtalk_stanza('terminate')
        else:
            iq, _ = self._jingle_stanza('session-terminate')
        self.stream.send(iq.toXml())


    def send_remote_candidates(self):

        iq, el = self._gtalk_stanza('candidates')
        for t in self.remote_transports:
            c = domish.Element((None, 'candidate'))
            c['generation'] = '0'
            c['network'] = '0'
            c['type'] = 'local'
            c['protocol'] = 'udp'
            c['preference'] = str(t[5])
            c['password'] = t[8]
            c['username'] = t[7]
            c['port'] = str(t[1])
            c['address'] = t[0]
            c['name'] = t[3].lower()
            el.addChild(c)

        self.stream.send(iq.toXml())

