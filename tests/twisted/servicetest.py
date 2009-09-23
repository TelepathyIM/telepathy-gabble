
"""
Infrastructure code for testing connection managers.
"""

from twisted.internet import glib2reactor
from twisted.internet.protocol import Protocol, Factory, ClientFactory
glib2reactor.install()
import sys

import pprint
import unittest

import dbus.glib

from twisted.internet import reactor

import constants as cs

tp_name_prefix = 'org.freedesktop.Telepathy'
tp_path_prefix = '/org/freedesktop/Telepathy'

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

class EventPattern:
    def __init__(self, type, **properties):
        self.type = type
        self.predicate = lambda x: True
        if 'predicate' in properties:
            self.predicate = properties['predicate']
            del properties['predicate']
        self.properties = properties

    def __repr__(self):
        properties = dict(self.properties)

        if self.predicate:
            properties['predicate'] = self.predicate

        return '%s(%r, **%r)' % (
            self.__class__.__name__, self.type, properties)

    def match(self, event):
        if event.type != self.type:
            return False

        for key, value in self.properties.iteritems():
            try:
                if getattr(event, key) != value:
                    return False
            except AttributeError:
                return False

        if self.predicate(event):
            return True

        return False


class TimeoutError(Exception):
    pass

class BaseEventQueue:
    """Abstract event queue base class.

    Implement the wait() method to have something that works.
    """

    def __init__(self, timeout=None):
        self.verbose = False
        self.forbidden_events = set()

        if timeout is None:
            self.timeout = 5
        else:
            self.timeout = timeout

    def log(self, s):
        if self.verbose:
            print s

    def log_event(self, event):
        if self.verbose:
            self.log('got event:')

            if self.verbose:
                map(self.log, format_event(event))

    def forbid_events(self, patterns):
        """
        Add patterns (an iterable of EventPattern) to the set of forbidden
        events. If a forbidden event occurs during an expect or expect_many,
        the test will fail.
        """
        self.forbidden_events.update(set(patterns))

    def unforbid_events(self, patterns):
        """
        Remove 'patterns' (an iterable of EventPattern) from the set of
        forbidden events. These must be the same EventPattern pointers that
        were passed to forbid_events.
        """
        self.forbidden_events.difference_update(set(patterns))

    def _check_forbidden(self, event):
        for e in self.forbidden_events:
            if e.match(event):
                print "forbidden event occurred:"
                for x in format_event(event):
                    print x
                assert False

    def expect(self, type, **kw):
        pattern = EventPattern(type, **kw)

        while True:
            event = self.wait()
            self.log_event(event)
            self._check_forbidden(event)

            if pattern.match(event):
                self.log('handled')
                self.log('')
                return event

            self.log('not handled')
            self.log('')

    def expect_many(self, *patterns):
        ret = [None] * len(patterns)

        while None in ret:
            try:
                event = self.wait()
            except TimeoutError:
                self.log('timeout')
                self.log('still expecting:')
                for i, pattern in enumerate(patterns):
                    if ret[i] is None:
                        self.log(' - %r' % pattern)
                raise
            self.log_event(event)
            self._check_forbidden(event)

            for i, pattern in enumerate(patterns):
                if ret[i] is None and pattern.match(event):
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
        self.log_event(event)

        if pattern.match(event):
            self.log('handled')
            self.log('')
            return event

        self.log('not handled')
        raise RuntimeError('expected %r, got %r' % (pattern, event))

class IteratingEventQueue(BaseEventQueue):
    """Event queue that works by iterating the Twisted reactor."""

    def __init__(self, timeout=None):
        BaseEventQueue.__init__(self, timeout)
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

    def test_expect_many2(self):
        # Test that events are only matched against patterns that haven't yet
        # been matched. This tests a regression.
        queue = TestEventQueue([Event('foo', x=1), Event('foo', x=2)])
        foo1, foo2 = queue.expect_many(
            EventPattern('foo'),
            EventPattern('foo'))
        assert foo1.type == 'foo' and foo1.x == 1
        assert foo2.type == 'foo' and foo2.x == 2

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

    if isinstance(x, dbus.Boolean):
        return bool(x)

    for t in [unicode, str, long, int, float]:
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
        test.handle_event(Event('dbus-error', method=method, error=err,
            name=err.get_dbus_name(), message=err.message))

    method_proxy = getattr(proxy, method)
    kw.update({'reply_handler': reply_func, 'error_handler': error_func})
    method_proxy(*args, **kw)

def sync_dbus(bus, q, conn):
    # Dummy D-Bus method call
    # This won't do the right thing unless the proxy has a unique name.
    assert conn.object.bus_name.startswith(':')
    root_object = bus.get_object(conn.object.bus_name, '/')
    call_async(
        q, dbus.Interface(root_object, 'org.freedesktop.DBus.Peer'), 'Ping')
    q.expect('dbus-return', method='Ping')

class ProxyWrapper:
    def __init__(self, object, default, others):
        self.object = object
        self.default_interface = dbus.Interface(object, default)
        self.Properties = dbus.Interface(object, dbus.PROPERTIES_IFACE)
        self.TpProperties = \
            dbus.Interface(object, tp_name_prefix + '.Properties')
        self.interfaces = dict([
            (name, dbus.Interface(object, iface))
            for name, iface in others.iteritems()])

    def __getattr__(self, name):
        if name in self.interfaces:
            return self.interfaces[name]

        if name in self.object.__dict__:
            return getattr(self.object, name)

        return getattr(self.default_interface, name)

def wrap_connection(conn):
    return ProxyWrapper(conn, tp_name_prefix + '.Connection',
        dict([
            (name, tp_name_prefix + '.Connection.Interface.' + name)
            for name in ['Aliasing', 'Avatars', 'Capabilities', 'Contacts',
              'Presence', 'SimplePresence', 'Requests']] +
        [('Peer', 'org.freedesktop.DBus.Peer'),
         ('ContactCapabilities', cs.CONN_IFACE_CONTACT_CAPS),
         ('Location', cs.CONN_IFACE_LOCATION),
        ]))

def wrap_channel(chan, type_, extra=None):
    interfaces = {
        type_: tp_name_prefix + '.Channel.Type.' + type_,
        'Group': tp_name_prefix + '.Channel.Interface.Group',
        }

    if extra:
        interfaces.update(dict([
            (name, tp_name_prefix + '.Channel.Interface.' + name)
            for name in extra]))

    return ProxyWrapper(chan, tp_name_prefix + '.Channel', interfaces)

def make_connection(bus, event_func, name, proto, params):
    cm = bus.get_object(
        tp_name_prefix + '.ConnectionManager.%s' % name,
        tp_path_prefix + '/ConnectionManager/%s' % name)
    cm_iface = dbus.Interface(cm, tp_name_prefix + '.ConnectionManager')

    connection_name, connection_path = cm_iface.RequestConnection(
        proto, params)
    conn = wrap_connection(bus.get_object(connection_name, connection_path))

    bus.add_signal_receiver(
        lambda *args, **kw:
            event_func(
                Event('dbus-signal',
                    path=unwrap(kw['path']),
                    signal=kw['member'], args=map(unwrap, args),
                    interface=kw['interface'])),
        None,       # signal name
        None,       # interface
        cm._named_service,
        path_keyword='path',
        member_keyword='member',
        interface_keyword='interface',
        byte_arrays=True
        )

    return conn

def make_channel_proxy(conn, path, iface):
    bus = dbus.SessionBus()
    chan = bus.get_object(conn.object.bus_name, path)
    chan = dbus.Interface(chan, tp_name_prefix + '.' + iface)
    return chan

# block_reading can be used if the test want to choose when we start to read
# data from the socket.
class EventProtocol(Protocol):
    def __init__(self, queue=None, block_reading=False):
        self.queue = queue
        self.block_reading = block_reading

    def dataReceived(self, data):
        if self.queue is not None:
            self.queue.handle_event(Event('socket-data', protocol=self,
                data=data))

    def sendData(self, data):
        self.transport.write(data)

    def connectionMade(self):
        if self.block_reading:
            self.transport.stopReading()

    def connectionLost(self, reason=None):
        if self.queue is not None:
            self.queue.handle_event(Event('socket-disconnected', protocol=self))

class EventProtocolFactory(Factory):
    def __init__(self, queue, block_reading=False):
        self.queue = queue
        self.block_reading = block_reading

    def _create_protocol(self):
        return EventProtocol(self.queue, self.block_reading)

    def buildProtocol(self, addr):
        proto = self._create_protocol()
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

def pretty(x):
    return pprint.pformat(unwrap(x))

def assertEquals(expected, value):
    if expected != value:
        raise AssertionError(
            "expected:\n%s\ngot:\n%s" % (pretty(expected), pretty(value)))

def assertNotEquals(expected, value):
    if expected == value:
        raise AssertionError(
            "expected something other than:\n%s" % pretty(value))

def assertContains(element, value):
    if element not in value:
        raise AssertionError(
            "expected:\n%s\nin:\n%s" % (pretty(element), pretty(value)))

def assertDoesNotContain(element, value):
    if element in value:
        raise AssertionError(
            "expected:\n%s\nnot in:\n%s" % (pretty(element), pretty(value)))

def assertLength(length, value):
    if len(value) != length:
        raise AssertionError("expected: length %d, got length %d:\n%s" % (
            length, len(value), pretty(value)))

def install_colourer():
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
    return sys.stdout

if __name__ == '__main__':
    unittest.main()

