from twisted.words.protocols.jabber.xmlstream import NS_STREAMS
from gabbletest import XmppAuthenticator, NS_XMPP_SASL, NS_XMPP_BIND
from base64 import b64decode, b64encode
from twisted.words.xish import domish
import constants as cs
from servicetest import ProxyWrapper, EventPattern, assertEquals

class SaslChannelWrapper(ProxyWrapper):
    def __init__(self, object, default=cs.CHANNEL, interfaces={
            "ServerAuthentication" : cs.CHANNEL_TYPE_SERVER_AUTHENTICATION,
            "SaslAuthentication" : cs.CHANNEL_IFACE_SASL_AUTH}):
        ProxyWrapper.__init__(self, object, default, interfaces)

class SaslComplexAuthenticator(XmppAuthenticator):
    def __init__(self, jid, exchange, mechanisms, data_on_success=True):
        XmppAuthenticator.__init__(self, jid, '')
        self._exchange = exchange
        self._data_on_success = data_on_success
        self._mechanisms = mechanisms
        self._stage = 0

    def streamStarted(self, root=None):
        if root:
            self.xmlstream.sid = root.getAttribute('id')

        self.xmlstream.sendHeader()

        if self.authenticated:
            # Initiator authenticated itself, and has started a new stream.

            features = domish.Element((NS_STREAMS, 'features'))
            bind = features.addElement((NS_XMPP_BIND, 'bind'))
            self.xmlstream.send(features)

            self.xmlstream.addOnetimeObserver(
                "/iq/bind[@xmlns='%s']" % NS_XMPP_BIND, self.bindIq)
        else:
            features = domish.Element((NS_STREAMS, 'features'))
            mechanisms = features.addElement((NS_XMPP_SASL, 'mechanisms'))
            for mechanism in self._mechanisms:
                mechanisms.addElement('mechanism', content=mechanism)
            self.xmlstream.send(features)

            self.xmlstream.addOnetimeObserver("/auth", self.auth)
            self.xmlstream.addObserver("/response", self.response)
            self.xmlstream.addObserver("/abort", self.abort)

    def _failure(self, fail_str):
        reply = domish.Element((NS_XMPP_SASL, 'failure'))
        reply.addElement(fail_str)

        self.xmlstream.send(reply)
        self.xmlstream.reset()

    def abort(self, elem):
        self._failure('aborted')

    def _send_challenge(self):
        challenge_str = self._exchange[self._stage][0]
        if self._data_on_success and self._stage == len(self._exchange) - 1:
            reply = domish.Element((NS_XMPP_SASL, 'success'))
            if challenge_str:
                reply.addContent(b64encode(challenge_str))
            self.xmlstream.send(reply)
            self.authenticated = True
            self.xmlstream.reset()
        else:
            reply = domish.Element((NS_XMPP_SASL, 'challenge'))
            reply.addContent(b64encode(challenge_str))
            self.xmlstream.send(reply)

    def _challenge(self, elem):
        if b64decode(str(elem)) != self._exchange[self._stage][1]:
            self._failure('not-authorized')
        elif not self._data_on_success and \
                self._stage == len(self._exchange) - 1:
            reply = domish.Element((NS_XMPP_SASL, 'success'))
            self.xmlstream.send(reply)
            self.authenticated = True
            self.xmlstream.reset()
        else:
            self._stage += 1
            self._send_challenge()


    def auth(self, auth):
        self._challenge(auth)

    def response(self, response):
        self._challenge(response)

class SaslPlainAuthenticator(XmppAuthenticator):
    def __init__(self, username, password):
        XmppAuthenticator.__init__(self, username, password)

    def auth(self, auth):
        reset = True # In PLAIN we always reset.
        try:
            user, passwd = b64decode(str(auth)).strip('\0').split('\0')
        except ValueError:
            reply = domish.Element((NS_XMPP_SASL, 'failure'))
            reply.addElement('incorrect-encoding')
        else:
            if (user, passwd) != (self.username, self.password):
                reply = domish.Element((NS_XMPP_SASL, 'failure'))
                reply.addElement('not-authorized')
            else:
                reply = domish.Element((NS_XMPP_SASL, 'success'))
                self.authenticated = True

        self.xmlstream.send(reply)

        if reset:
            self.xmlstream.reset()

def connect_and_get_sasl_channel(q, bus, conn):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    old_signal, new_signal = q.expect_many(
            EventPattern('dbus-signal', signal='NewChannel'),
            EventPattern('dbus-signal', signal='NewChannels'))

    path, type, handle_type, handle, suppress_handler = old_signal.args

    chan = SaslChannelWrapper(bus.get_object(conn.bus_name, path))

    auth_info = {}
    avail_mechs = []
    for obj_path, props in new_signal.args[0]:
        if props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_SERVER_AUTHENTICATION:
            assertEquals (props[cs.AUTH_METHOD], cs.AUTH_TYPE_SASL)
            info = props[cs.AUTH_INFO]
            mechs = props[cs.SASL_AVAILABLE_MECHANISMS]
            return chan, info, mechs

    raise AssertionError, "SASL channel not created."

def abort_auth(q, chan, reason, message):
    reason_err_map = {cs.SASL_ABORT_REASON_USER_ABORT :
                      "org.freedesktop.Telepathy.Error.Cancelled",
                      cs.SASL_ABORT_REASON_INVALID_CHALLENGE :
                      "org.freedesktop.Telepathy.Error.Sasl.InvalidReply"}

    chan.SaslAuthentication.Abort(reason, message)

    q.expect_many(
        EventPattern(
            'dbus-signal', signal='StateChanged',
            interface=cs.CHANNEL_IFACE_SASL_AUTH,
            args=[cs.SASL_STATUS_CLIENT_FAILED,
                  reason_err_map[reason],
                  message]),
        EventPattern(
            'dbus-signal', signal="StatusChanged",
            args=[cs.CONN_STATUS_DISCONNECTED,
                  cs.CSR_AUTHENTICATION_FAILED]))
