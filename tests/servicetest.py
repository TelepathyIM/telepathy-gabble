
"""
Infrastructure code for testing Gabble by pretending to be a Jabber server.
"""

from twisted.internet import glib2reactor
glib2reactor.install()

import pprint
import sys
import traceback

import dbus
import dbus.glib

from twisted.internet import reactor

tp_name_prefix = 'org.freedesktop.Telepathy'
tp_path_prefix = '/org/freedesktop/Telepathy'

class TryNextHandler(Exception):
    pass

def Eventually(func):
    def handler(event, data):
        if func(event, data):
            return True
        else:
            raise TryNextHandler()
    handler.__name__ = func.__name__
    return handler

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
        except TryNextHandler, e:
            if len(self.queue) > 1:
                missed = self.queue.pop(0)
                self.handle_event(event)
                self.queue.insert(0, missed)
            return
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


def create_test(name, proto, params):
    test = {}

    test['bus'] = dbus.SessionBus()
    test['cm'] = test['bus'].get_object(
        tp_name_prefix + '.ConnectionManager.%s' % name,
        tp_path_prefix + '/ConnectionManager/%s' % name)
    test['cm_iface'] = dbus.Interface(test['cm'],
        tp_name_prefix + '.ConnectionManager')

    connection_name, connection_path = test['cm_iface'].RequestConnection(
        proto, params)
    test['conn'] = test['cm']._bus.get_object(connection_name, connection_path)

    test['conn_iface'] = dbus.Interface(test['conn'], tp_name_prefix + '.Connection')

    test['handler'] = EventTest()

    test['bus'].add_signal_receiver(
        handler_function=lambda *args, **kw:
            test['handler'].handle_event((
                'dbus-signal', unwrap(kw['path']), kw['member'],
                map(unwrap, args))),

        named_service=test['cm']._named_service,
        path_keyword='path',
        member_keyword='member'
        )

    test['handler'].data = test
    test['test'] = test
    return test['handler']


def run_test(handler):
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

    handler.verbose = False
    for arg in sys.argv:
        if arg == '-v':
            handler.verbose = True

    map(handler.expect, funcs)
    handler.data['conn'].Connect()
    reactor.run()

