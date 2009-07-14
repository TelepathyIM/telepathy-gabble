
"""
Test registration.
"""

from gabbletest import (
    exec_test, make_result_iq, acknowledge_iq, send_error_reply,
    )

from twisted.words.xish import domish, xpath

import ns
import constants as cs

def connect_and_send_form(q, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    event = q.expect('stream-iq', query_ns=ns.REGISTER)
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query.addElement('username')
    query.addElement('password')

    stream.send(result)

    event = q.expect('stream-iq')
    iq = event.stanza
    assert xpath.queryForString('/iq/query/username', iq) == 'test'
    assert xpath.queryForString('/iq/query/password', iq) == 'pass'

    return iq

def test_success(q, bus, conn, stream):
    iq = connect_and_send_form(q, conn, stream)
    acknowledge_iq(stream, iq)

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

def test_conflict(q, bus, conn, stream):
    iq = connect_and_send_form(q, conn, stream)

    error = domish.Element((None, 'error'))
    error['code'] = '409'
    error['type'] = 'cancel'
    error.addElement((ns.STANZA, 'conflict'))
    send_error_reply(stream, iq, error)

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_NAME_IN_USE])

def test_with_email(q, bus, conn, stream):
    # The form requires <email/>; so, Gabble should give up.
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    event = q.expect('stream-iq', query_ns=ns.REGISTER)
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query.addElement('username')
    query.addElement('password')
    query.addElement('email')

    stream.send(result)

    # AuthenticationFailed is the closest ConnectionStatusReason to "I tried
    # but couldn't register you an account."
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_AUTHENTICATION_FAILED])

def test_data_forms(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    event = q.expect('stream-iq', query_ns=ns.REGISTER)
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query.addElement((None, 'instructions')).addChild("Hope you like x:data")
    # Use the same element names as in jabber:iq:register as per XEP 0077's
    # Extensibility section.
    x = query.addElement((ns.X_DATA, 'x'))
    x['type'] = 'form'
    x.addElement((None, 'title')).addChild("Account Registration")
    x.addElement((None, 'instructions')).addChild(
        "This is gratuitously a data form!")

    form_type = x.addElement((None, 'field'))
    form_type['type'] = 'hidden'
    form_type['var'] = 'FORM_TYPE'
    form_type.addElement((None, 'value')).addChild(ns.REGISTER)

    first = x.addElement((None, 'field'))
    first['type'] = 'text-single'
    first['label'] = 'Username'
    first['var'] = 'username'
    first.addElement((None, 'required'))

    first = x.addElement((None, 'field'))
    first['type'] = 'text-single'
    first['label'] = 'Password'
    first['var'] = 'password'
    first.addElement((None, 'required'))

    stream.send(result)

    # AuthenticationFailed is the closest ConnectionStatusReason to "I tried
    # but couldn't register you an account."
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_AUTHENTICATION_FAILED])

def test_redirection(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    event = q.expect('stream-iq', query_ns=ns.REGISTER)
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query.addElement((None, 'instructions')).addChild("Sigh.")
    # Tell the user to go to some website
    url = query.addElement((ns.X_OOB, 'x')).addElement((None, 'url'))
    url.addChild("http://foogle.talk.example/newaccount")

    stream.send(result)

    # AuthenticationFailed is the closest ConnectionStatusReason to "I tried
    # but couldn't register you an account."
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_AUTHENTICATION_FAILED])

if __name__ == '__main__':
    exec_test(test_success, {'register': True})
    exec_test(test_conflict, {'register': True})
    exec_test(test_with_email, {'register': True})
    exec_test(test_data_forms, {'register': True})
    exec_test(test_redirection, {'register': True})

