
"""
Infrastructure code for testing Gabble by pretending to be a Jabber server.
"""

import pprint
import sys
import traceback

import dbus
import dbus.glib

from twisted.words.xish import xpath
from twisted.words.protocols.jabber.client import IQ
from twisted.words.protocols.jabber import xmlstream
from twisted.internet import reactor

tp_name_prefix = 'org.freedesktop.Telepathy'
tp_path_prefix = '/org/freedesktop/Telepathy'

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

class EventTest:
    """Somewhat odd event dispatcher for asynchronous tests.

    Callbacks are kept in a queue. Incoming events are passed to the first
    callback. If the callback returns True, the callback is removed. If the
    callback raises AssertionError, the test fails. If there are no more
    callbacks, the test passes. The reactor is stopped when the test passes.
    """

    def __init__(self):
        self.queue = []
        self.data = {'test': self}
        self.timeout_delayed_call = reactor.callLater(5, self.timeout_cb)
        #self.verbose = True
        self.verbose = False
        # ugh
        self.stopping = False

    def timeout_cb(self):
        print 'timed out waiting for events'
        print self.queue[0]
        self.fail()

    def fail(self):
        # ugh; better way to stop the reactor and exit(1)?
        import os
        os._exit(1)

    def expect(self, f):
        self.queue.append(f)

    def try_stop(self):
        if self.stopping:
            return True

        if not self.queue:
            if self.verbose:
                print 'no handlers left; stopping'

            self.stopping = True
            reactor.stop()
            return True

        return False

    def handle_event(self, event):
        if self.try_stop():
            return

        if self.verbose:
            print 'got event:'

            for item in event:
                print '- %s' % pprint.pformat(item)

        try:
            ret = self.queue[0](event, self.data)
        except AssertionError, e:
            print 'test failed:'
            traceback.print_exc()
            self.fail()
        except Exception, e:
            print 'error in handler:'
            traceback.print_exc()
            self.fail()

        if ret not in (True, False):
            print ("warning: %s() returned something other than True or False"
                % self.queue[0].__name__)

        if ret:
            self.queue.pop(0)
            self.timeout_delayed_call.reset(5)

            if self.verbose:
                print 'event handled'

                if self.queue:
                    print 'next handler: %r' % self.queue[0]
        else:
            if self.verbose:
                print 'event not handled'

        if self.verbose:
            print

        self.try_stop()

def cm_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix + '.ConnectionManager')

def conn_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix + '.Connection')

def get_cm(bus, cm):
    return bus.get_object(
        tp_name_prefix + '.ConnectionManager.%s' % cm,
        tp_path_prefix + '/ConnectionManager/%s' % cm)

def request_connection(cm, protocol, parameters):
    connection_name, connection_path = cm_iface(cm).RequestConnection(
        protocol, parameters)
    connection = cm._bus.get_object(connection_name, connection_path)
    return connection

def unwrap(x):
    """Hack to unwrap D-Bus values, so that they're easier to read when
    printed."""

    if isinstance(x, list):
        return map(unwrap, x)

    if isinstance(x, tuple):
        return tuple(map(unwrap, x))

    if isinstance(x, dict):
        return dict([(unwrap(k), unwrap(v)) for k, v in x.iteritems()])

    for t in [unicode, str, long, int, float, bool]:
        if isinstance(x, t):
            return t(x)

    return x

def call_async(test, proxy, method, *args, **kw):
    """Call a D-Bus method asynchronously and generate an event for the
    resulting method return/error."""

    def reply_func(*ret):
        test.handle_event(('dbus-return', method) + ret)

    def error_func(err):
        test.handle_event(('dbus-error', method, err))

    method_proxy = getattr(proxy, method)
    kw.update({'reply_handler': reply_func, 'error_handler': error_func})
    method_proxy(*args, **kw)

def gabble_test_setup(handler, params=None):
    # set up Gabble
    bus = dbus.SessionBus()
    gabble = get_cm(bus, 'gabble')
    default_params = {
        'account': 'test@localhost/Resource',
        'password': 'pass',
        'resource': 'Resource',
        'server': 'localhost',
        'port': dbus.UInt32(4242),
        }

    if params:
        default_params.update(params)

    connection = request_connection(gabble, 'jabber', default_params)

    # listen for D-Bus signals
    bus.add_signal_receiver(
        lambda *args, **kw:
            handler.handle_event((
                'dbus-signal', unwrap(kw['path']), kw['member'],
                map(unwrap, args))),
        None, None, gabble._named_service,
        path_keyword='path',
        member_keyword='member',
        byte_arrays=True,
        )

    # set up Jabber server
    authenticator = Authenticator(
        'test', '364321e78f46562a65a902156e03c322badbcf48')
    factory = XmlStreamFactory(handler, authenticator)
    reactor.listenTCP(4242, factory)

    # update callback data
    handler.data['conn'] = connection
    handler.data['factory'] = factory

    # go!
    connection.Connect()

def run(test, params=None):
    for arg in sys.argv:
        if arg == '-v':
            test.verbose = True

    gabble_test_setup(test, params)
    reactor.run()

def go(params=None):
    """Create a test from the top level functions named expect_* in the
    __main__ module and run it.
    """

    path, _, _, _ = traceback.extract_stack()[0]
    import compiler
    import __main__
    ast = compiler.parseFile(path)
    funcs = [
        getattr(__main__, node.name)
        for node in ast.node.asList()
        if node.__class__ == compiler.ast.Function and
            node.name.startswith('expect_')]
    test = EventTest()
    map(test.expect, funcs)
    run(test, params)

