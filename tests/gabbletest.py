
"""
Infrastructure code for testing Gabble by pretending to be a Jabber server.
"""

import base64
import sha

import servicetest
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
        result.addElement((query.uri, 'query'))

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
    queries = xpath.queryForNodes("/iq/query", iq)

    if queries:
        event.query = queries[0]
        event.query_ns = event.query.uri

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
            servicetest.Event('stream-message', stanza=x)))
        self.addObserver('//presence', lambda x: event_func(
            make_stream_event('stream-presence', x)))
        self.addObserver('//event/stream/authd', self._cb_authd)

    def _cb_authd(self, _):
        # called when stream is authenticated
        self.addObserver(
            "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
            self._cb_disco_iq)
        self.event_func(servicetest.Event('stream-authenticated'))

    def _cb_disco_iq(self, iq):
        if iq['to'] == 'localhost':
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

class XmlStreamFactory(xmlstream.XmlStreamFactory):
    def __init__(self, event_func, authenticator):
        xmlstream.XmlStreamFactory.__init__(self, authenticator)
        self.event_func = event_func

    def buildProtocol(self, _):
        # XXX: This is necessary because xmlstream.XmlStreamFactory's
        # buildProtocol doesn't honour self.protocol. This is broken in
        # Twisted Words 2.5, fixed in SVN.
        self.resetDelay()
        # create the stream
        xs = self.protocol(self.event_func, self.authenticator)
        xs.factory = self
        # register all the bootstrap observers.
        for event, fn in self.bootstraps:
            xs.addObserver(event, fn)
        return xs

def go(params=None, authenticator=None, protocol=None, start=None):
    # hack to ease debugging
    domish.Element.__repr__ = domish.Element.toXml

    default_params = {
        'account': 'test@localhost/Resource',
        'password': 'pass',
        'resource': 'Resource',
        'server': 'localhost',
        'port': dbus.UInt32(4242),
        }

    if params:
        default_params.update(params)

    handler = servicetest.EventTest()
    data = servicetest.prepare_test(handler.handle_event, 'gabble', 'jabber',
        default_params)
    handler.data.update(data)

    if authenticator is None:
        authenticator = JabberAuthenticator('test', 'pass')

    if protocol is None:
        protocol = JabberXmlStream

    class Stream(protocol):
        def __init__(self, *args):
            protocol.__init__(self, *args)
            handler.data['stream'] = self

    # set up Jabber server
    factory = XmlStreamFactory(handler.handle_event, authenticator)
    factory.protocol = Stream
    reactor.listenTCP(4242, factory)

    # go!
    servicetest.run_test(handler, start)


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


