"""
Test incoming error messages in MUC channels.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import exec_test, make_muc_presence, request_muc_handle
from servicetest import call_async, EventPattern
import ns

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    room_handle = request_muc_handle(q, conn, stream, 'chat@conf.localhost')
    call_async(q, conn, 'RequestChannel',
        'org.freedesktop.Telepathy.Channel.Type.Text', 2, room_handle, True)

    gfc, _, _ = q.expect_many(
        EventPattern('dbus-signal', signal='GroupFlagsChanged'),
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [], [], [], [2], 0, 0]),
        EventPattern('stream-presence', to='chat@conf.localhost/test'))
    assert gfc.args[1] == 0

    # Send presence for other member of room.
    stream.send(make_muc_presence(
        'owner', 'moderator', 'chat@conf.localhost', 'bob'))

    # Send presence for own membership of room.
    stream.send(make_muc_presence(
        'none', 'participant', 'chat@conf.localhost', 'test'))

    event = q.expect('dbus-signal', signal='MembersChanged',
        args=[u'', [2, 3], [], [], [], 0, 0])
    assert conn.InspectHandles(1, [2]) == ['chat@conf.localhost/test']
    assert conn.InspectHandles(1, [3]) == ['chat@conf.localhost/bob']

    event = q.expect('dbus-return', method='RequestChannel')
    text_chan = bus.get_object(conn.bus_name, event.value[0])


    # Suppose we don't have permission to speak in this MUC.  Send a message to
    # the channel, and have the MUC reject it as unauthorized.
    content = u"hi r ther ne warez n this chanel?"
    greeting = [
        dbus.Dictionary({ }, signature='sv'),
        { 'content-type': 'text/plain',
          'content': content,
        }
    ]

    dbus.Interface(text_chan,
        u'org.freedesktop.Telepathy.Channel.Interface.Messages'
        ).SendMessage(greeting, dbus.UInt32(0))

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
    assert sender == 0, old_received.args
    assert type == 4, old_received.args # Message_Type_Delivery_Report
    assert flags == 2, old_received.args # Non_Text_Content
    assert text == '', old_received.args

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
    # Gabble doesn't issue tokens for messages you send, so no token should be
    # in the report
    assert 'delivery-token' not in part, part

    # Check that the included echo is from us, and matches all the keys in the
    # message we sent.
    assert 'delivery-echo' in part, part
    echo = part['delivery-echo']
    assert len(echo) == len(greeting), (echo, greeting)
    # Earlier in this test we checked that handle 2 is us.
    assert echo[0]['message-sender'] == 2, echo[0]
    for i in range(0, len(echo)):
        for key in greeting[i]:
            assert key in echo[i], (i, key, echo)
            assert echo[i][key] == greeting[i][key], (i, key, echo, greeting)


    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True

if __name__ == '__main__':
    exec_test(test)

