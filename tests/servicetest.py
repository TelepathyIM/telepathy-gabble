
"""
Infrastructure code for testing connection managers.
"""

from twisted.internet import glib2reactor
from twisted.internet.protocol import Protocol, Factory, ClientFactory
glib2reactor.install()

import pprint
import traceback
import unittest

import dbus.glib

from twisted.internet import reactor

tp_name_prefix = 'org.freedesktop.Telepathy'
tp_path_prefix = '/org/freedesktop/Telepathy'

class TryNextHandler(Exception):
    pass

def lazy(func):
    def handler(event, data):
        if func(event, data):
            return True
        else:
            raise TryNextHandler()
    handler.__name__ = func.__name__
    return handler

def match(type, **kw):
    def decorate(func):
        def handler(event, data, *extra, **extra_kw):
            if event.type != type:
                return False

            for key, value in kw.iteritems():
                if not hasattr(event, key):
                    return False

                if getattr(event, key) != value:
                    return False

            return func(event, data, *extra, **extra_kw)

        handler.__name__ = func.__name__
        return handler

    return decorate

class Event:
    def __init__(self, type, **kw):
        self.__dict__.update(kw)
        self.type = type

def format_event(event):
    ret = ['- type %s' % event.type]

    for key in dir(event):
        if key != 'type' and not key.startswith('_'):
            ret.append('- %s: %s' % (
                key, pprint.pformat(getattr(event, key))))

            if key == 'error':
                ret.append('%s' % getattr(event, key))

    return ret

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

    def log(self, s):
        if self.verbose:
            print s

    def try_stop(self):
        if self.stopping:
            return True

        if not self.queue:
            self.log('no handlers left; stopping')
            self.stopping = True
            reactor.stop()
            return True

        return False

    def call_handlers(self, event):
        self.log('trying %r' % self.queue[0])
        handler = self.queue.pop(0)

        try:
            ret = handler(event, self.data)
            if not ret:
                self.queue.insert(0, handler)
        except TryNextHandler, e:
            if self.queue:
                ret = self.call_handlers(event)
            else:
                ret = False
            self.queue.insert(0, handler)

        return ret

    def handle_event(self, event):
        if self.try_stop():
            return

        self.log('got event:')
        self.log('- type: %s' % event.type)
        map(self.log, format_event(event))

        try:
            ret = self.call_handlers(event)
        except SystemExit, e:
            if e.code:
                print "Unsuccessful exit:", e
                self.fail()
            else:
                self.queue[:] = []
                ret = True
        except AssertionError, e:
            print 'test failed:'
            traceback.print_exc()
            self.fail()
        except (Exception, KeyboardInterrupt), e:
            print 'error in handler:'
            traceback.print_exc()
            self.fail()

        if ret not in (True, False):
            print ("warning: %s() returned something other than True or False"
                % self.queue[0].__name__)

        if ret:
            self.timeout_delayed_call.reset(5)
            self.log('event handled')
        else:
            self.log('event not handled')

        self.log('')
        self.try_stop()

class EventPattern:
    def __init__(self, type, **properties):
        self.type = type
        self.properties = properties

    def match(self, event):
        if event.type != self.type:
            return False

        for key, value in self.properties.iteritems():
            try:
                if getattr(event, key) != value:
                    return False
            except AttributeError:
                return False

        return True

class TimeoutError(Exception):
    pass

class BaseEventQueue:
    """Abstract event queue base class.

    Implement the wait() method to have something that works.
    """

    timeout = 5

    def __init__(self):
        self.verbose = False

    def log(self, s):
        if self.verbose:
            print s

    def expect(self, type, **kw):
        pattern = EventPattern(type, **kw)

        while True:
            event = self.wait()
            self.log('got event:')
            map(self.log, format_event(event))

            if pattern.match(event):
                self.log('handled')
                self.log('')
                return event

            self.log('not handled')
            self.log('')

    def expect_many(self, *patterns):
        ret = [None] * len(patterns)

        while None in ret:
            event = self.wait()
            self.log('got event:')
            map(self.log, format_event(event))

            for i, pattern in enumerate(patterns):
                if pattern.match(event):
                    self.log('handled')
                    self.log('')
                    ret[i] = event
                    break
            else:
                self.log('not handled')
                self.log('')

        return ret

    def demand(self, type, **kw):
        pattern = EventPattern(type, **kw)

        event = self.wait()
        self.log('got event:')
        map(self.log, format_event(event))

        if pattern.match(event):
            self.log('handled')
            self.log('')
            return event

        self.log('not handled')
        raise RuntimeError('expected %r, got %r' % (pattern, event))

class IteratingEventQueue(BaseEventQueue):
    """Event queue that works by iterating the Twisted reactor."""

    def __init__(self):
        BaseEventQueue.__init__(self)
        self.events = []

    def wait(self):
        stop = [False]

        def later():
            stop[0] = True

        delayed_call = reactor.callLater(self.timeout, later)

        while (not self.events) and (not stop[0]):
            reactor.iterate(0.1)

        if self.events:
            delayed_call.cancel()
            return self.events.pop(0)
        else:
            raise TimeoutError

    def append(self, event):
        self.events.append(event)

    # compatibility
    handle_event = append

class TestEventQueue(BaseEventQueue):
    def __init__(self, events):
        BaseEventQueue.__init__(self)
        self.events = events

    def wait(self):
        if self.events:
            return self.events.pop(0)
        else:
            raise TimeoutError

class EventQueueTest(unittest.TestCase):
    def test_expect(self):
        queue = TestEventQueue([Event('foo'), Event('bar')])
        assert queue.expect('foo').type == 'foo'
        assert queue.expect('bar').type == 'bar'

    def test_expect_many(self):
        queue = TestEventQueue([Event('foo'), Event('bar')])
        bar, foo = queue.expect_many(
            EventPattern('bar'),
            EventPattern('foo'))
        assert bar.type == 'bar'
        assert foo.type == 'foo'

    def test_timeout(self):
        queue = TestEventQueue([])
        self.assertRaises(TimeoutError, queue.expect, 'foo')

    def test_demand(self):
        queue = TestEventQueue([Event('foo'), Event('bar')])
        foo = queue.demand('foo')
        assert foo.type == 'foo'

    def test_demand_fail(self):
        queue = TestEventQueue([Event('foo'), Event('bar')])
        self.assertRaises(RuntimeError, queue.demand, 'bar')

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
        test.handle_event(Event('dbus-return', method=method,
            value=unwrap(ret)))

    def error_func(err):
        test.handle_event(Event('dbus-error', method=method, error=err))

    method_proxy = getattr(proxy, method)
    kw.update({'reply_handler': reply_func, 'error_handler': error_func})
    method_proxy(*args, **kw)


class ProxyWrapper:
    def __init__(self, object, default, others):
        self.object = object
        self.default_interface = dbus.Interface(object, default)
        self.interfaces = dict([
            (name, dbus.Interface(object, iface))
            for name, iface in others.iteritems()])

    def __getattr__(self, name):
        if name in self.interfaces:
            return self.interfaces[name]

        if name in self.object.__dict__:
            return getattr(self.object, name)

        return getattr(self.default_interface, name)

def prepare_test(event_func, name, proto, params):
    bus = dbus.SessionBus()
    cm = bus.get_object(
        tp_name_prefix + '.ConnectionManager.%s' % name,
        tp_path_prefix + '/ConnectionManager/%s' % name)
    cm_iface = dbus.Interface(cm, tp_name_prefix + '.ConnectionManager')

    connection_name, connection_path = cm_iface.RequestConnection(
        proto, params)
    conn = bus.get_object(connection_name, connection_path)
    conn = ProxyWrapper(conn, tp_name_prefix + '.Connection',
        dict([
            (name, tp_name_prefix + '.Connection.Interface.' + name)
            for name in ['Aliasing', 'Avatars', 'Capabilities', 'Presence']] +
        [('Peer', 'org.freedesktop.DBus.Peer')]))

    bus.add_signal_receiver(
        lambda *args, **kw:
            event_func(
                Event('dbus-signal',
                    path=unwrap(kw['path'])[len(tp_path_prefix):],
                    signal=kw['member'], args=map(unwrap, args))),
        None,       # signal name
        None,       # interface
        cm._named_service,
        path_keyword='path',
        member_keyword='member',
        byte_arrays=True
        )

    return bus, conn

def load_event_handlers():
    path, _, _, _ = traceback.extract_stack()[0]
    import compiler
    import __main__
    ast = compiler.parseFile(path)
    return [
        getattr(__main__, node.name)
        for node in ast.node.asList()
        if node.__class__ == compiler.ast.Function and
            node.name.startswith('expect_')]

class EventProtocol(Protocol):
    def __init__(self, queue=None):
        self.queue = queue

    def dataReceived(self, data):
        if self.queue is not None:
            self.queue.handle_event(Event('socket-data', protocol=self,
                data=data))

    def sendData(self, data):
        self.transport.write(data)

class EventProtocolFactory(Factory):
    def __init__(self, queue):
        self.queue = queue

    def buildProtocol(self, addr):
        proto =  EventProtocol(self.queue)
        self.queue.handle_event(Event('socket-connected', protocol=proto))
        return proto

class EventProtocolClientFactory(EventProtocolFactory, ClientFactory):
    pass

def watch_tube_signals(q, tube):
    def got_signal_cb(*args, **kwargs):
        q.handle_event(Event('tube-signal',
            path=kwargs['path'],
            signal=kwargs['member'],
            args=map(unwrap, args),
            tube=tube))

    tube.add_signal_receiver(got_signal_cb,
        path_keyword='path', member_keyword='member',
        byte_arrays=True)

if __name__ == '__main__':
    unittest.main()

