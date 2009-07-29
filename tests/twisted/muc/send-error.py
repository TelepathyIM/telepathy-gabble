"""
Test incoming error messages in MUC channels.
"""

import dbus

from gabbletest import exec_test
from servicetest import EventPattern
import constants as cs
import ns

from mucutil import join_muc_and_check

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    muc = 'chat@conf.localhost'
    _, text_chan, test_handle, bob_handle = \
        join_muc_and_check(q, bus, conn, stream, muc)

    # Suppose we don't have permission to speak in this MUC.  Send a message to
    # the channel, and have the MUC reject it as unauthorized.
    content = u"hi r ther ne warez n this chanel?"
    greeting = [
        dbus.Dictionary({ }, signature='sv'),
        { 'content-type': 'text/plain',
          'content': content,
        }
    ]

    sent_token = dbus.Interface(text_chan, cs.CHANNEL_IFACE_MESSAGES) \
        .SendMessage(greeting, dbus.UInt32(0))

    stream_message, _, _ = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='Sent'),
        EventPattern('dbus-signal', signal='MessageSent'),
        )

    # computer says no
    elem = stream_message.stanza
    elem['from'] = 'chat@conf.localhost'
    elem['to'] = 'chat@conf.localhost/test'
    elem['type'] = 'error'
    error = elem.addElement('error')
    error['code'] = '401'
    error['type'] = 'auth'
    error.addElement((ns.STANZA, 'not-authorized'))

    stream.send(elem)

    # check that we got a failed delivery report and a SendError
    send_error, received, message_received = q.expect_many(
        EventPattern('dbus-signal', signal='SendError'),
        EventPattern('dbus-signal', signal='Received'),
        EventPattern('dbus-signal', signal='MessageReceived'),
        )

    PERMISSION_DENIED = 3

    err, timestamp, type, text = send_error.args
    assert err == PERMISSION_DENIED, send_error.args
    # there's no way to tell when the original message was sent from the error stanza
    assert timestamp == 0, send_error.args
    # Gabble can't determine the type of the original message; see muc/test-muc.py
    # assert type == 0, send_error.args
    assert text == content, send_error.args

    # The Text.Received signal should be a "you're not tall enough" stub
    id, timestamp, sender, type, flags, text = received.args
    assert sender == 0, received.args
    assert type == 4, received.args # Message_Type_Delivery_Report
    assert flags == 2, received.args # Non_Text_Content
    assert text == '', received.args

    # Check that the Messages.MessageReceived signal was a failed delivery report
    assert len(message_received.args) == 1, message_received.args
    parts = message_received.args[0]
    # The delivery report should just be a header, no body.
    assert len(parts) == 1, parts
    part = parts[0]
    # The intended recipient was the MUC, so there's no contact handle
    # suitable for being 'message-sender'.
    assert 'message-sender' not in part or part['message-sender'] == 0, part
    assert part['message-type'] == 4, part # Message_Type_Delivery_Report
    assert part['delivery-status'] == 3, part # Delivery_Status_Permanently_Failed
    assert part['delivery-error'] == PERMISSION_DENIED, part
    assert part['delivery-token'] == sent_token, part

    # Check that the included echo is from us, and matches all the keys in the
    # message we sent.
    assert 'delivery-echo' in part, part
    echo = part['delivery-echo']
    assert len(echo) == len(greeting), (echo, greeting)
    assert echo[0]['message-sender'] == test_handle, echo[0]
    assert echo[0]['message-token'] == sent_token, echo[0]
    for i in range(0, len(echo)):
        for key in greeting[i]:
            assert key in echo[i], (i, key, echo)
            assert echo[i][key] == greeting[i][key], (i, key, echo, greeting)

if __name__ == '__main__':
    exec_test(test)
