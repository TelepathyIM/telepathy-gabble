
"""
Infrastructure code for testing connection managers.
"""

from twisted.internet import glib2reactor
from twisted.internet.protocol import Protocol, Factory, ClientFactory
glib2reactor.install()
import sys
import time

import pprint
import unittest

import dbus.glib

from twisted.internet import reactor

import constants as cs

tp_name_prefix = 'org.freedesktop.Telepathy'
tp_path_prefix = '/org/freedesktop/Telepathy'

class DictionarySupersetOf (object):
    """Utility class for expecting "a dictionary with at least these keys"."""
    def __init__(self, dictionary):
        self._dictionary = dictionary
    def __repr__(self):
        return "DictionarySupersetOf(%s)" % self._dictionary
    def __eq__(self, other):
        """would like to just do:
        return set(other.items()).issuperset(self._dictionary.items())
        but it turns out that this doesn't work if you have another dict
        nested in the values of your dicts"""
        try:
            for k,v in self._dictionary.items():
                if k not in other or other[k] != v:
                    return False
            return True
        except TypeError: # other is not iterable
            return False

class Event:
    def __init__(self, type, **kw):
        self.__dict__.update(kw)
        self.type = type
        (self.subqueue, self.subtype) = type.split ("-", 1)

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
        self.predicate = None
        if 'predicate' in properties:
            self.predicate = properties['predicate']
            del properties['predicate']
        self.properties = properties
        (self.subqueue, self.subtype) = type.split ("-", 1)

    def __repr__(self):
        properties = dict(self.properties)

        if self.predicate is not None:
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

        if self.predicate is None or self.predicate(event):
            return True

        return False


class TimeoutError(Exception):
    pass

class ForbiddenEventOccurred(Exception):
    def __init__(self, event):
        Exception.__init__(self)
        self.event = event

    def __str__(self):
        return '\n' + '\n'.join(format_event(self.event))

class BaseEventQueue:
    """Abstract event queue base class.

    Implement the wait() method to have something that works.
    """

    def __init__(self, timeout=None):
        self.verbose = False
        self.forbidden_events = set()
        self.event_queues = {}

        if timeout is None:
            self.timeout = 5
        else:
            self.timeout = timeout

    def log(self, s):
        if self.verbose:
            print s

    def log_queues(self, queues):
        self.log ("Waiting for event on: %s" % ", ".join(queues))

    def log_event(self, event):
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
                raise ForbiddenEventOccurred(event)

    def expect(self, type, **kw):
        """
        Waits for an event matching the supplied pattern to occur, and returns
        it. For example, to await a D-Bus signal with particular arguments:

            e = q.expect('dbus-signal', signal='Badgers', args=["foo", 42])
        """
        pattern = EventPattern(type, **kw)
        t = time.time()

        while True:
            event = self.wait([pattern.subqueue])
            self._check_forbidden(event)

            if pattern.match(event):
                self.log('handled, took %0.3f ms'
                    % ((time.time() - t) * 1000.0) )
                self.log('')
                return event

            self.log('not handled')
            self.log('')

    def expect_many(self, *patterns):
        """
        Waits for events matching all of the supplied EventPattern instances to
        return, and returns a list of events in the same order as the patterns
        they matched. After a pattern is successfully matched, it is not
        considered for future events; if more than one unsatisfied pattern
        matches an event, the first "wins".

        Note that the expected events may occur in any order. If you're
        expecting a series of events in a particular order, use repeated calls
        to expect() instead.

        This method is useful when you're awaiting a number of events which may
        happen in any order. For instance, in telepathy-gabble, calling a D-Bus
        method often causes a value to be returned immediately, as well as a
        query to be sent to the server. Since these events may reach the test
        in either order, the following is incorrect and will fail if the IQ
        happens to reach the test first:

            ret = q.expect('dbus-return', method='Foo')
            query = q.expect('stream-iq', query_ns=ns.FOO)

        The following would be correct:

            ret, query = q.expect_many(
                EventPattern('dbus-return', method='Foo'),
                EventPattern('stream-iq', query_ns=ns.FOO),
            )
        """
        ret = [None] * len(patterns)
        t = time.time()

        while None in ret:
            try:
                queues = set()
                for i, pattern in enumerate(patterns):
                    if ret[i] is None:
                        queues.add(pattern.subqueue)
                event = self.wait(queues)
            except TimeoutError:
                self.log('timeout')
                self.log('still expecting:')
                for i, pattern in enumerate(patterns):
                    if ret[i] is None:
                        self.log(' - %r' % pattern)
                raise
            self._check_forbidden(event)

            for i, pattern in enumerate(patterns):
                if ret[i] is None and pattern.match(event):
                    self.log('handled, took %0.3f ms'
                        % ((time.time() - t) * 1000.0) )
                    self.log('')
                    ret[i] = event
                    break
            else:
                self.log('not handled')
                self.log('')

        return ret

    def demand(self, type, **kw):
        pattern = EventPattern(type, **kw)

        event = self.wait([pattern.subqueue])

        if pattern.match(event):
            self.log('handled')
            self.log('')
            return event

        self.log('not handled')
        raise RuntimeError('expected %r, got %r' % (pattern, event))

    def queues_available(self, queues):
        if queues == None:
            return self.event_queues.keys()
        else:
            available = self.event_queues.keys()
            return filter(lambda x: x in available, queues)


    def pop_next(self, queue):
        events = self.event_queues[queue]
        e = events.pop(0)
        if not events:
           self.event_queues.pop (queue)
        return e

    def append(self, event):
        self.log ("Adding to queue")
        self.log_event (event)
        self.event_queues[event.subqueue] = \
            self.event_queues.get(event.subqueue, []) + [event]

class IteratingEventQueue(BaseEventQueue):
    """Event queue that works by iterating the Twisted reactor."""

    def __init__(self, timeout=None):
        BaseEventQueue.__init__(self, timeout)

    def wait(self, queues=None):
        stop = [False]

        def later():
            stop[0] = True

        delayed_call = reactor.callLater(self.timeout, later)

        self.log_queues(queues)

        qa = self.queues_available(queues)
        while not qa and (not stop[0]):
            reactor.iterate(0.01)
            qa = self.queues_available(queues)

        if qa:
            delayed_call.cancel()
            e = self.pop_next (qa[0])
            self.log_event (e)
            return e
        else:
            raise TimeoutError

class TestEventQueue(BaseEventQueue):
    def __init__(self, events):
        BaseEventQueue.__init__(self)
        for e in events:
            self.append (e)

    def wait(self, queues = None):
        qa = self.queues_available(queues)

        if qa:
            return self.pop_next (qa[0])
        else:
            raise TimeoutError

class EventQueueTest(unittest.TestCase):
    def test_expect(self):
        queue = TestEventQueue([Event('test-foo'), Event('test-bar')])
        assert queue.expect('test-foo').type == 'test-foo'
        assert queue.expect('test-bar').type == 'test-bar'

    def test_expect_many(self):
        queue = TestEventQueue([Event('test-foo'),
            Event('test-bar')])
        bar, foo = queue.expect_many(
            EventPattern('test-bar'),
            EventPattern('test-foo'))
        assert bar.type == 'test-bar'
        assert foo.type == 'test-foo'

    def test_expect_many2(self):
        # Test that events are only matched against patterns that haven't yet
        # been matched. This tests a regression.
        queue = TestEventQueue([Event('test-foo', x=1), Event('test-foo', x=2)])
        foo1, foo2 = queue.expect_many(
            EventPattern('test-foo'),
            EventPattern('test-foo'))
        assert foo1.type == 'test-foo' and foo1.x == 1
        assert foo2.type == 'test-foo' and foo2.x == 2

    def test_expect_queueing(self):
        queue = TestEventQueue([Event('foo-test', x=1),
            Event('foo-test', x=2)])

        queue.append(Event('bar-test', x=1))
        queue.append(Event('bar-test', x=2))

        queue.append(Event('baz-test', x=1))
        queue.append(Event('baz-test', x=2))

        for x in xrange(1,2):
            e = queue.expect ('baz-test')
            assertEquals (x, e.x)

            e = queue.expect ('bar-test')
            assertEquals (x, e.x)

            e = queue.expect ('foo-test')
            assertEquals (x, e.x)

    def test_timeout(self):
        queue = TestEventQueue([])
        self.assertRaises(TimeoutError, queue.expect, 'test-foo')

    def test_demand(self):
        queue = TestEventQueue([Event('test-foo'), Event('test-bar')])
        foo = queue.demand('test-foo')
        assert foo.type == 'test-foo'

    def test_demand_fail(self):
        queue = TestEventQueue([Event('test-foo'), Event('test-bar')])
        self.assertRaises(RuntimeError, queue.demand, 'test-bar')

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
        test.append(Event('dbus-return', method=method,
            value=unwrap(ret)))

    def error_func(err):
        test.append(Event('dbus-error', method=method, error=err,
            name=err.get_dbus_name(), message=str(err)))

    method_proxy = getattr(proxy, method)
    kw.update({'reply_handler': reply_func, 'error_handler': error_func})
    method_proxy(*args, **kw)

def sync_dbus(bus, q, conn):
    # Dummy D-Bus method call
    # This won't do the right thing unless the proxy has a unique name.
    assert conn.object.bus_name.startswith(':')
    root_object = bus.get_object(conn.object.bus_name, '/')
    call_async(
        q, dbus.Interface(root_object, 'org.freedesktop.Telepathy.Tests'), 'DummySyncDBus')
    q.expect('dbus-error', method='DummySyncDBus')

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
         ('ContactInfo', cs.CONN_IFACE_CONTACT_INFO),
         ('Location', cs.CONN_IFACE_LOCATION),
         ('Future', tp_name_prefix + '.Connection.FUTURE'),
         ('MailNotification', cs.CONN_IFACE_MAIL_NOTIFICATION),
         ('ContactList', cs.CONN_IFACE_CONTACT_LIST),
         ('ContactGroups', cs.CONN_IFACE_CONTACT_GROUPS),
         ('PowerSaving', cs.CONN_IFACE_POWER_SAVING),
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
            self.queue.append(Event('socket-data', protocol=self,
                data=data))

    def sendData(self, data):
        self.transport.write(data)

    def connectionMade(self):
        if self.block_reading:
            self.transport.stopReading()

    def connectionLost(self, reason=None):
        if self.queue is not None:
            self.queue.append(Event('socket-disconnected', protocol=self))

class EventProtocolFactory(Factory):
    def __init__(self, queue, block_reading=False):
        self.queue = queue
        self.block_reading = block_reading

    def _create_protocol(self):
        return EventProtocol(self.queue, self.block_reading)

    def buildProtocol(self, addr):
        proto = self._create_protocol()
        self.queue.append(Event('socket-connected', protocol=proto))
        return proto

class EventProtocolClientFactory(EventProtocolFactory, ClientFactory):
    pass

def watch_tube_signals(q, tube):
    def got_signal_cb(*args, **kwargs):
        q.append(Event('tube-signal',
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

def assertSameSets(expected, value):
    exp_set = set(expected)
    val_set = set(value)

    if exp_set != val_set:
        raise AssertionError(
            "expected contents:\n%s\ngot:\n%s" % (
                pretty(exp_set), pretty(val_set)))

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

def assertFlagsSet(flags, value):
    masked = value & flags
    if masked != flags:
        raise AssertionError(
            "expected flags %u, of which only %u are set in %u" % (
            flags, masked, value))

def assertFlagsUnset(flags, value):
    masked = value & flags
    if masked != 0:
        raise AssertionError(
            "expected none of flags %u, but %u are set in %u" % (
            flags, masked, value))

def assertDBusError(name, error):
    if error.get_dbus_name() != name:
        raise AssertionError(
            "expected DBus error named:\n  %s\ngot:\n  %s\n(with message: %s)"
            % (name, error.get_dbus_name(), error.message))

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
            for p, f in self.patterns.items():
                if s.startswith(p):
                    self.fh.write(f(p) + s[len(p):])
                    return

            self.fh.write(s)

    sys.stdout = Colourer(sys.stdout, patterns)
    return sys.stdout

if __name__ == '__main__':
    unittest.main()

