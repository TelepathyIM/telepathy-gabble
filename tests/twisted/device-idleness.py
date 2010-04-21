"""
Test listening to device idleness status changes.

When we're not building with MCE support, Gabble uses a test suite-specific
service on the session bus. When we *are* building with MCE support, it uses
the system bus, so we can't test it. Hence:
"""

import config

if config.HAVE_MCE:
    print "NOTE: built with real MCE support; skipping idleness test"
    raise SystemExit(77)

from functools import partial

from gabbletest import exec_test, GoogleXmlStream
from servicetest import call_async, Event, assertEquals
import ns

import dbus
import dbus.service

# Fake MCE constants, cloned from slacker.c
MCE_SERVICE = "org.freedesktop.Telepathy.Gabble.Tests.MCE"

MCE_SIGNAL_IF = "org.freedesktop.Telepathy.Gabble.Tests.MCE"
MCE_INACTIVITY_SIG = "InactivityChanged"

MCE_REQUEST_IF = "org.freedesktop.Telepathy.Gabble.Tests.MCE"
MCE_REQUEST_PATH = "/org/freedesktop/Telepathy/Gabble/Tests/MCE"
MCE_INACTIVITY_STATUS_GET = "GetInactivity"

from twisted.internet import reactor

class FakeMCE(dbus.service.Object):
    def __init__(self, q, bus, inactive=False):
        super(FakeMCE, self).__init__(bus, MCE_REQUEST_PATH)

        self.q = q
        self.inactive = inactive

    @dbus.service.method(dbus_interface=MCE_REQUEST_IF,
        in_signature='', out_signature='b')
    def GetInactivity(self):
        self.q.append(Event('get-inactivity-called'))
        return self.inactive

    @dbus.service.signal(dbus_interface=MCE_SIGNAL_IF, signature='b')
    def InactivityChanged(self, new_value):
        self.inactive = new_value


def expect_command(q, inactive):
    event = q.expect('stream-iq', query_name='query', query_ns=ns.GOOGLE_QUEUE)
    command = event.query.firstChildElement()
    assertEquals('enable' if inactive else 'disable', command.name)

def test(q, bus, conn, stream, initially_inactive=False):
    mce = FakeMCE(q, bus, initially_inactive)

    call_async(q, conn, 'Connect')
    q.expect('get-inactivity-called')

    if initially_inactive:
        expect_command(q, True)
    else:
        mce.InactivityChanged(True)
        expect_command(q, True)

    mce.InactivityChanged(False)
    expect_command(q, False)

    # Just cycle it a bit to check it doesn't blow up slowly
    mce.InactivityChanged(True)
    expect_command(q, True)

    mce.InactivityChanged(False)
    expect_command(q, False)

    mce.remove_from_connection()

if __name__ == '__main__':
    dbus.SessionBus().request_name(MCE_SERVICE, 0)
    try:
        exec_test(partial(test, initially_inactive=False), protocol=GoogleXmlStream)
        exec_test(partial(test, initially_inactive=True), protocol=GoogleXmlStream)
    finally:
        dbus.SessionBus().release_name(MCE_SERVICE)
