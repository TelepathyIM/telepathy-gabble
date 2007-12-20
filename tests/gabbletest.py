
"""
Infrastructure code for testing Gabble by pretending to be a Jabber server.
"""

import base64
import os
import sha
import sys

import servicetest
import twisted
from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ
from twisted.words.protocols.jabber import xmlstream
from twisted.internet import reactor

import dbus

NS_XMPP_SASL = 'urn:ietf:params:xml:ns:xmpp-sasl'
NS_XMPP_BIND = 'urn:ietf:params:xml:ns:xmpp-bind'

def make_result_iq(stream, iq):
    result = IQ(stream, "result")
    result["id"] = iq["id"]
    query = iq.firstChildElement()

    if query:
        result.addElement((query.uri, query.name))

    return result

def acknowledge_iq(stream, iq):
    stream.send(make_result_iq(stream, iq))

class JabberAuthenticator(xmlstream.Authenticator):
    "Trivial XML stream authenticator that accepts one username/digest pair."

    def __init__(self, username, password):
        self.username = username
        self.password = password
        xmlstream.Authenticator.__init__(self)

    def streamStarted(self):
        self.xmlstream.sendHeader()
        self.xmlstream.addOnetimeObserver(
            "/iq/query[@xmlns='jabber:iq:auth']", self.initialIq)

    def initialIq(self, iq):
        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        query = result.addElement('query')
        query["xmlns"] = "jabber:iq:auth"
        query.addElement('username', content='test')
        query.addElement('password')
        query.addElement('digest')
        query.addElement('resource')
        self.xmlstream.addOnetimeObserver('/iq/query/username', self.secondIq)
        self.xmlstream.send(result)

    def secondIq(self, iq):
        username = xpath.queryForNodes('/iq/query/username', iq)
        assert map(str, username) == [self.username]

        digest = xpath.queryForNodes('/iq/query/digest', iq)
        expect = sha.sha(self.xmlstream.sid + self.password).hexdigest()
        assert map(str, digest) == [expect]

        resource = xpath.queryForNodes('/iq/query/resource', iq)
        assert map(str, resource) == ['Resource']

        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        self.xmlstream.send(result)
        self.xmlstream.dispatch(self.xmlstream, xmlstream.STREAM_AUTHD_EVENT)

class XmppAuthenticator(xmlstream.Authenticator):
    def __init__(self, username, password):
        xmlstream.Authenticator.__init__(self)
        self.username = username
        self.password = password
        self.authenticated = False

    def streamStarted(self):
        self.xmlstream.sendHeader()

        if self.authenticated:
            # Initiator authenticated itself, and has started a new stream.

            features = domish.Element((xmlstream.NS_STREAMS, 'features'))
            bind = features.addElement((NS_XMPP_BIND, 'bind'))
            self.xmlstream.send(features)

            self.xmlstream.addOnetimeObserver(
                "/iq/bind[@xmlns='%s']" % NS_XMPP_BIND, self.bindIq)
        else:
            features = domish.Element((xmlstream.NS_STREAMS, 'features'))
            mechanisms = features.addElement((NS_XMPP_SASL, 'mechanisms'))
            mechanism = mechanisms.addElement('mechanism', content='PLAIN')
            self.xmlstream.send(features)

            self.xmlstream.addOnetimeObserver("/auth", self.auth)

    def auth(self, auth):
        assert (base64.b64decode(str(auth)) ==
            '\x00%s\x00%s' % (self.username, self.password))

        success = domish.Element((NS_XMPP_SASL, 'success'))
        self.xmlstream.send(success)
        self.xmlstream.reset()
        self.authenticated = True

    def bindIq(self, iq):
        assert xpath.queryForString('/iq/bind/resource', iq) == 'Resource'

        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        bind = result.addElement((NS_XMPP_BIND, 'bind'))
        jid = bind.addElement('jid', content='test@localhost/Resource')
        self.xmlstream.send(result)

        self.xmlstream.dispatch(self.xmlstream, xmlstream.STREAM_AUTHD_EVENT)

def make_stream_event(type, stanza):
    event = servicetest.Event(type, stanza=stanza)
    event.to = stanza.getAttribute("to")
    return event

def make_iq_event(iq):
    event = make_stream_event('stream-iq', iq)
    event.iq_type = iq.getAttribute("type")
    query = iq.firstChildElement()

    if query:
        event.query = query
        event.query_ns = query.uri
        event.query_name = query.name

    return event

def make_presence_event(stanza):
    event = make_stream_event('stream-presence', stanza)
    event.presence_type = stanza.getAttribute('type')
    return event

def make_message_event(stanza):
    event = make_stream_event('stream-message', stanza)
    event.message_type = stanza.getAttribute('type')
    return event

class BaseXmlStream(xmlstream.XmlStream):
    initiating = False
    namespace = 'jabber:client'

    def __init__(self, event_func, authenticator):
        xmlstream.XmlStream.__init__(self, authenticator)
        self.event_func = event_func
        self.addObserver('//iq', lambda x: event_func(
            make_iq_event(x)))
        self.addObserver('//message', lambda x: event_func(
            make_message_event(x)))
        self.addObserver('//presence', lambda x: event_func(
            make_presence_event(x)))
        self.addObserver('//event/stream/authd', self._cb_authd)

    def _cb_authd(self, _):
        # called when stream is authenticated
        self.addObserver(
            "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
            self._cb_disco_iq)
        self.event_func(servicetest.Event('stream-authenticated'))

    def _cb_disco_iq(self, iq):
        if iq.getAttribute('to') == 'localhost':
            # add PEP support
            nodes = xpath.queryForNodes(
                "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
                iq)
            query = nodes[0]
            identity = query.addElement('identity')
            identity['category'] = 'pubsub'
            identity['type'] = 'pep'

            iq['type'] = 'result'
            self.send(iq)

class JabberXmlStream(BaseXmlStream):
    version = (0, 9)

class XmppXmlStream(BaseXmlStream):
    version = (1, 0)

def prepare_test(event_func, params=None, authenticator=None, protocol=None):
    default_params = {
        'account': 'test@localhost/Resource',
        'password': 'pass',
        'resource': 'Resource',
        'server': 'localhost',
        'port': dbus.UInt32(4242),
        }

    if params:
        default_params.update(params)

    bus, conn = servicetest.prepare_test(event_func, 'gabble', 'jabber',
        default_params)

    # set up Jabber server

    if authenticator is None:
        authenticator = JabberAuthenticator('test', 'pass')

    if protocol is None:
        protocol = JabberXmlStream

    stream = protocol(event_func, authenticator)
    factory = twisted.internet.protocol.Factory()
    factory.protocol = lambda *args: stream
    reactor.listenTCP(4242, factory)
    return bus, conn, stream

def go(params=None, authenticator=None, protocol=None, start=None):
    # hack to ease debugging
    domish.Element.__repr__ = domish.Element.toXml

    handler = servicetest.EventTest()
    bus, conn, stream = \
        prepare_test(handler.handle_event, params, authenticator, protocol)
    handler.data = {
        'bus': bus,
        'conn': conn,
        'conn_iface': dbus.Interface(conn,
            'org.freedesktop.Telepathy.Connection'),
        'stream': stream}
    handler.data['test'] = handler
    handler.verbose = (os.environ.get('CHECK_TWISTED_VERBOSE', '') != '')
    map(handler.expect, servicetest.load_event_handlers())

    if '-v' in sys.argv:
        handler.verbose = True

    if start is None:
        handler.data['conn'].Connect()
    else:
        start(handler.data)

    reactor.run()

def exec_test(fun, params=None):
    queue = servicetest.IteratingEventQueue()

    if '-v' in sys.argv:
        queue.verbose = True

    bus, conn, stream = prepare_test(queue.append, params)

    # hack to ease debugging
    domish.Element.__repr__ = domish.Element.toXml

    if sys.stdout.isatty():
        def red(s):
            return '\x1b[31m%s\x1b[0m' % s

        def green(s):
            return '\x1b[32m%s\x1b[0m' % s

        patterns = {
            'handled': green,
            'not handled': red,
            }

        class Colourer:
            def __init__(self, fh, patterns):
                self.fh = fh
                self.patterns = patterns

            def write(self, s):
                f = self.patterns.get(s, lambda x: x)
                self.fh.write(f(s))

        sys.stdout = Colourer(sys.stdout, patterns)

    try:
        fun(queue, bus, conn, stream)
    finally:
        try:
            conn.Disconnect()
            # second call destroys object
            conn.Disconnect()
        except dbus.DBusException, e:
            pass

# Useful routines for server-side vCard handling
current_vcard = domish.Element(('vcard-temp', 'vCard'))

def handle_get_vcard(event, data):
    iq = event.stanza

    if iq['type'] != 'get':
        return False

    if iq.uri != 'jabber:client':
        return False

    vcard = list(iq.elements())[0]

    if vcard.name != 'vCard':
        return False

    # Send back current vCard
    new_iq = IQ(data['stream'], 'result')
    new_iq['id'] = iq['id']
    new_iq.addChild(current_vcard)
    data['stream'].send(new_iq)
    return True

def handle_set_vcard(event, data):
    global current_vcard
    iq = event.stanza

    if iq['type'] != 'set':
        return False

    if iq.uri != 'jabber:client':
        return False

    vcard = list(iq.elements())[0]

    if vcard.name != 'vCard':
        return False

    current_vcard = iq.firstChildElement()

    new_iq = IQ(data['stream'], 'result')
    new_iq['id'] = iq['id']
    data['stream'].send(new_iq)
    return True


