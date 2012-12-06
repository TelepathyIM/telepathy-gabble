# hey, Python: encoding: utf-8
from gabbletest import XmppAuthenticator
from base64 import b64decode, b64encode
from twisted.words.xish import domish
import constants as cs
import ns
from servicetest import (ProxyWrapper, EventPattern, assertEquals,
        assertLength, Event)

class SaslChannelWrapper(ProxyWrapper):
    def __init__(self, object, default=cs.CHANNEL, interfaces={
            "ServerAuthentication" : cs.CHANNEL_TYPE_SERVER_AUTHENTICATION,
            "SASLAuthentication" : cs.CHANNEL_IFACE_SASL_AUTH}):
        ProxyWrapper.__init__(self, object, default, interfaces)

class SaslEventAuthenticator(XmppAuthenticator):
    def __init__(self, jid, mechanisms):
        XmppAuthenticator.__init__(self, jid, '')
        self._mechanisms = mechanisms

    def streamSASL(self):
        XmppAuthenticator.streamSASL(self)

        self.xmlstream.addObserver("/response", self._response)
        self.xmlstream.addObserver("/abort", self._abort)

    def failure(self, fail_str):
        reply = domish.Element((ns.NS_XMPP_SASL, 'failure'))
        reply.addElement(fail_str)
        self.xmlstream.send(reply)
        self.xmlstream.reset()

    def abort(self):
        self.failure('abort')

    def not_authorized(self):
        self.failure('not-authorized')

    def success(self, data=None):
        reply = domish.Element((ns.NS_XMPP_SASL, 'success'))

        if data is not None:
            reply.addContent(b64encode(data))

        self.xmlstream.send(reply)
        self.authenticated=True
        self.xmlstream.reset()

    def challenge(self, data):
        reply = domish.Element((ns.NS_XMPP_SASL, 'challenge'))
        reply.addContent(b64encode(data))
        self.xmlstream.send(reply)

    def auth(self, auth):
        # Special case in XMPP: '=' means a zero-byte blob, whereas an empty
        # or self-terminating XML element means no initial response.
        # (RFC 3920 §6.2 (3))
        if str(auth) == '':
            self._event_func(Event('sasl-auth', authenticator=self,
                has_initial_response=False,
                initial_response=None,
                xml=auth))
        elif str(auth) == '=':
            self._event_func(Event('sasl-auth', authenticator=self,
                has_initial_response=False,
                initial_response=None,
                xml=auth))
        else:
            self._event_func(Event('sasl-auth', authenticator=self,
                has_initial_response=True,
                initial_response=b64decode(str(auth)),
                xml=auth))

    def _response(self, response):
        self._event_func(Event('sasl-response', authenticator=self,
            response=b64decode(str(response)),
            xml=response))

    def _abort(self, abort):
        self._event_func(Event('sasl-abort', authenticator=self,
            xml=abort))

def connect_and_get_sasl_channel(q, bus, conn):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    return expect_sasl_channel(q, bus, conn)

def expect_sasl_channel(q, bus, conn):
    old_signal, new_signal = q.expect_many(
            EventPattern('dbus-signal', signal='NewChannel',
                predicate=lambda e:
                    e.args[1] == cs.CHANNEL_TYPE_SERVER_AUTHENTICATION),
            EventPattern('dbus-signal', signal='NewChannels',
                predicate=lambda e:
                    e.args[0][0][1].get(cs.CHANNEL_TYPE) ==
                        cs.CHANNEL_TYPE_SERVER_AUTHENTICATION),
                )

    path, type, handle_type, handle, suppress_handler = old_signal.args

    chan = SaslChannelWrapper(bus.get_object(conn.bus_name, path))
    assertLength(1, new_signal.args[0])
    assertEquals(path, new_signal.args[0][0][0])
    props = new_signal.args[0][0][1]

    assertEquals(cs.CHANNEL_IFACE_SASL_AUTH, props.get(cs.AUTH_METHOD))
    return chan, props

def abort_auth(q, chan, reason, message):
    reason_err_map = {
        cs.SASL_ABORT_REASON_USER_ABORT : cs.CANCELLED,
        cs.SASL_ABORT_REASON_INVALID_CHALLENGE : cs.SERVICE_CONFUSED }

    mapped_error = reason_err_map.get(reason, cs.CANCELLED)

    chan.SASLAuthentication.AbortSASL(reason, message)

    ssc, ce, _ = q.expect_many(
        EventPattern(
            'dbus-signal', signal='SASLStatusChanged',
            interface=cs.CHANNEL_IFACE_SASL_AUTH,
            predicate=lambda e: e.args[0] == cs.SASL_STATUS_CLIENT_FAILED),
        EventPattern('dbus-signal', signal='ConnectionError'),
        EventPattern(
            'dbus-signal', signal="StatusChanged",
            args=[cs.CONN_STATUS_DISCONNECTED,
                  cs.CSR_AUTHENTICATION_FAILED]))

    assertEquals(cs.SASL_STATUS_CLIENT_FAILED, ssc.args[0])
    assertEquals(mapped_error, ssc.args[1])
    assertEquals(message, ssc.args[2].get('debug-message')),
    assertEquals(mapped_error, ce.args[0])
