
"""
Infrastructure code for testing Gabble by pretending to be a Jabber server.
"""

import servicetest
from twisted.words.xish import xpath
from twisted.words.protocols.jabber.client import IQ
from twisted.words.protocols.jabber import xmlstream
from twisted.internet import reactor

import dbus

class Authenticator(xmlstream.Authenticator):
    "Trivial XML stream authenticator that accepts one username/digest pair."

    def __init__(self, expected_username, expected_digest):
        self.expected_username = expected_username
        self.expected_digest = expected_digest
        xmlstream.Authenticator.__init__(self)

    def connectionMade(self):
        self.xmlstream.initiating = False
        self.xmlstream.sid = '1'
        self.xmlstream.sendHeader()

    def streamStarted(self):
        self.xmlstream.addOnetimeObserver("/iq", self.initialIq)

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
        assert map(str, username) == [self.expected_username]

        digest = xpath.queryForNodes('/iq/query/digest', iq)
        assert map(str, digest) == [self.expected_digest]

        resource = xpath.queryForNodes('/iq/query/resource', iq)
        assert map(str, resource) == ['Resource']

        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        self.xmlstream.send(result)
        self.xmlstream.dispatch(self.xmlstream, xmlstream.STREAM_AUTHD_EVENT)

class XmlStream(xmlstream.XmlStream):
    def __init__(self, handler, authenticator):
        xmlstream.XmlStream.__init__(self, authenticator)
        self.handler = handler
        handler.data['stream'] = self
        self.addObserver('//iq', lambda x: handler.handle_event(
            ('stream-iq', x)))
        self.addObserver('//message', lambda x: handler.handle_event(
            ('stream-message', x)))
        self.addObserver('//presence', lambda x: handler.handle_event(
            ('stream-presence', x)))
        self.addObserver('//event/stream/authd', self._cb_authd)

    def _cb_authd(self, _):
        # caleld when stream is authenticated
        self.addObserver(
            "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
            self._cb_disco_iq)
        self.handler.handle_event(('stream-authenticated',))

    def _cb_disco_iq(self, iq):
        if iq['to'] == 'localhost':
            # no features
            iq['type'] = 'result'
            self.send(iq)

class XmlStreamFactory(xmlstream.XmlStreamFactory):
    def __init__(self, handler, authenticator):
        xmlstream.XmlStreamFactory.__init__(self, authenticator)
        self.handler = handler

    def buildProtocol(self, _):
        # XXX: This is necessary because xmlstream.XmlStreamFactory's
        # buildProtocol doesn't honour self.protocol. This is fixed in SVN.
        self.resetDelay()
        # create the stream
        xs = XmlStream(self.handler, self.authenticator)
        xs.factory = self
        # register all the bootstrap observers.
        for event, fn in self.bootstraps:
            xs.addObserver(event, fn)
        return xs

def go(params=None):
    default_params = {
        'account': 'test@localhost/Resource',
        'password': 'pass',
        'resource': 'Resource',
        'server': 'localhost',
        'port': dbus.UInt32(4242),
        }

    if params:
        default_params.update(params)

    handler = servicetest.create_test('gabble', 'jabber', default_params)

    # set up Jabber server
    authenticator = Authenticator(
        'test', '364321e78f46562a65a902156e03c322badbcf48')
    factory = XmlStreamFactory(handler, authenticator)
    reactor.listenTCP(4242, factory)

    # update callback data
    handler.data['factory'] = factory

    # go!
    servicetest.run_test(handler)

