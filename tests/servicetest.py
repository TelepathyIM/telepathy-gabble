
"""
Infrastructure code for testing connection managers.
"""

from twisted.internet import glib2reactor
glib2reactor.install()

import pprint
import os
import sys
import traceback

import dbus
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
            self.timeout_delayed_call.reset(5)
            self.log('event handled')
        else:
            self.log('event not handled')

        self.log('')
        self.try_stop()

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
        test.handle_event(Event('dbus-return', method=method, value=ret))

    def error_func(err):
        test.handle_event(Event('dbus-error', method=method, error=err))

    method_proxy = getattr(proxy, method)
    kw.update({'reply_handler': reply_func, 'error_handler': error_func})
    method_proxy(*args, **kw)


def prepare_test(event_func, name, proto, params):
    bus = dbus.SessionBus()
    cm = bus.get_object(
        tp_name_prefix + '.ConnectionManager.%s' % name,
        tp_path_prefix + '/ConnectionManager/%s' % name)
    cm_iface = dbus.Interface(cm, tp_name_prefix + '.ConnectionManager')

    connection_name, connection_path = cm_iface.RequestConnection(
        proto, params)
    conn = bus.get_object(connection_name, connection_path)
    conn_iface = dbus.Interface(conn, tp_name_prefix + '.Connection')
    data = {}

    for name in ('bus', 'cm', 'cm_iface', 'conn', 'conn_iface'):
        data[name] = locals()[name]

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

    return data

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

def run_test(handler, start=None):
    """Create a test from the top level functions named expect_* in the
    __main__ module and run it.
    """

    handler.verbose = (os.environ.get('CHECK_TWISTED_VERBOSE', '') != '')
    for arg in sys.argv:
        if arg == '-v':
            handler.verbose = True

    map(handler.expect, load_event_handlers())

    if start is None:
        handler.data['conn'].Connect()
    else:
        start(handler.data)

    reactor.run()
