"""
Test the server sasl channel with Jabber auth pseudomechanisms
"""

from twisted.words.xish import domish
from twisted.words.protocols.jabber import xmlstream
from twisted.words.protocols.jabber.client import IQ

import hashlib

import dbus

from servicetest import (EventPattern, assertEquals, assertSameSets,
        assertContains)
from gabbletest import exec_test, JabberXmlStream, JabberAuthenticator
import constants as cs
import ns
from saslutil import expect_sasl_channel, abort_auth

JID = "test@localhost"
PASSWORD = "pass"

def test_jabber_pass_success(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    e = q.expect('auth-initial-iq')
    authenticator = e.authenticator
    authenticator.respondToInitialIq(e.iq)

    chan, props = expect_sasl_channel(q, bus, conn)

    assertSameSets(['X-TELEPATHY-PASSWORD'],
            props.get(cs.SASL_AVAILABLE_MECHANISMS))

    chan.SASLAuthentication.StartMechanismWithData('X-TELEPATHY-PASSWORD',
            PASSWORD)

    e = q.expect('auth-second-iq')
    authenticator.respondToSecondIq(e.iq)

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SERVER_SUCCEEDED, '', {}])

    chan.SASLAuthentication.AcceptSASL()

    q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             args=[cs.SASL_STATUS_SUCCEEDED, '', {}])

    e = q.expect('dbus-signal', signal='StatusChanged',
                 args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

CODES = { 'not-authorized': 401,
    'conflict': 409,
    'not-acceptable': 406 }
TYPES = { 'not-authorized': 'auth',
    'conflict': 'cancel',
    'not-acceptable': 'modify' }
CSRS = { 'not-authorized': cs.CSR_AUTHENTICATION_FAILED,
    'conflict': cs.CSR_NAME_IN_USE,
    'not-acceptable': cs.CSR_AUTHENTICATION_FAILED }
ERRORS = { 'not-authorized': cs.AUTHENTICATION_FAILED,
    'conflict': cs.ALREADY_CONNECTED,
    'not-acceptable': cs.AUTHENTICATION_FAILED }

def test_jabber_pass_fail(q, bus, conn, stream, which):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    e = q.expect('auth-initial-iq')
    authenticator = e.authenticator
    authenticator.respondToInitialIq(e.iq)

    chan, props = expect_sasl_channel(q, bus, conn)

    assertSameSets(['X-TELEPATHY-PASSWORD'],
            props.get(cs.SASL_AVAILABLE_MECHANISMS))

    chan.SASLAuthentication.StartMechanismWithData('X-TELEPATHY-PASSWORD',
            PASSWORD)

    e = q.expect('auth-second-iq')

    result = IQ(stream, 'error')
    result['id'] = e.id
    error = result.addElement('error')
    error['code'] = str(CODES[which])
    error['type'] = TYPES[which]
    error.addElement((ns.STANZA, which))
    stream.send(result)

    e = q.expect('dbus-signal', signal='SASLStatusChanged',
             interface=cs.CHANNEL_IFACE_SASL_AUTH,
             predicate=lambda e: e.args[0] == cs.SASL_STATUS_SERVER_FAILED)
    assertEquals(ERRORS[which], e.args[1])
    assertContains('debug-message', e.args[2])

    e = q.expect('dbus-signal', signal='ConnectionError')
    assertEquals(ERRORS[which], e.args[0])
    assertContains('debug-message', e.args[1])

    e = q.expect('dbus-signal', signal='StatusChanged')
    assertEquals(cs.CONN_STATUS_DISCONNECTED, e.args[0])
    assertEquals(CSRS[which], e.args[1])

def test_jabber_pass_not_authorized(q, bus, conn, stream):
    test_jabber_pass_fail(q, bus, conn, stream, 'not-authorized')

def test_jabber_pass_conflict(q, bus, conn, stream):
    test_jabber_pass_fail(q, bus, conn, stream, 'conflict')

def test_jabber_pass_not_acceptable(q, bus, conn, stream):
    test_jabber_pass_fail(q, bus, conn, stream, 'not-acceptable')

if __name__ == '__main__':
    for test in (
            # these are Examples 5 to 8 of XEP-0078
            test_jabber_pass_success, test_jabber_pass_not_authorized,
            test_jabber_pass_conflict, test_jabber_pass_not_acceptable):
        exec_test(
            test, {'password': None, 'account' : JID},
            protocol=JabberXmlStream,
            authenticator=JabberAuthenticator(JID.split('@')[0], PASSWORD,
                emit_events=True))
