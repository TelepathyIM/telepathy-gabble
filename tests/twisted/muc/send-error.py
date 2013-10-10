"""
Test incoming error messages in MUC channels.
"""

import dbus

from gabbletest import exec_test
from servicetest import EventPattern, assertEquals, assertLength, assertContains
import constants as cs
import ns

from mucutil import join_muc_and_check

def test(q, bus, conn, stream):
    muc = 'chat@conf.localhost'
    text_chan, test_handle, bob_handle = \
        join_muc_and_check(q, bus, conn, stream, muc)

    # Suppose we don't have permission to speak in this MUC.  Send a message to
    # the channel, and have the MUC reject it as unauthorized.
    send_message_and_expect_error(q, stream,
        text_chan, test_handle, bob_handle,
        u"hi r ther ne warez n this chanel?",
        '401', 'auth', 'not-authorized',
        delivery_status=cs.DELIVERY_STATUS_PERMANENTLY_FAILED,
        send_error_value=cs.SendError.PERMISSION_DENIED)

    # This time, we get rate-limited.
    # <https://bugs.freedesktop.org/show_bug.cgi?id=43166>
    send_message_and_expect_error(q, stream,
        text_chan, test_handle, bob_handle,
        "faster faster",
        '500', 'wait', 'resource-constraint',
        delivery_status=cs.DELIVERY_STATUS_TEMPORARILY_FAILED,
        # Yuck this isn't a very good name is it?
        send_error_value=cs.SendError.TOO_LONG)

    # How about an error message in the reply? This is from Prosody. See
    # https://bugs.freedesktop.org/show_bug.cgi?id=43166#c9
    send_message_and_expect_error(q, stream,
        text_chan, test_handle, bob_handle,
        content=u"fair enough",
        code=None,
        type_='wait',
        element='policy-violation',
        error_message='The room is currently overactive, please try again later',
        delivery_status=cs.DELIVERY_STATUS_TEMPORARILY_FAILED,
        # Maybe we should expand the SendError codes some day, because this one
        # is l-a-m-e.
        send_error_value=cs.SendError.PERMISSION_DENIED)


def send_message_and_expect_error(q, stream,
                                  text_chan, test_handle, bob_handle,
                                  content,
                                  code=None,
                                  type_=None,
                                  element=None,
                                  error_message=None,
                                  delivery_status=None,
                                  send_error_value=None):
    greeting = [
        dbus.Dictionary({ }, signature='sv'),
        { 'content-type': 'text/plain',
          'content': content,
        }
    ]

    sent_token = text_chan.Text.SendMessage(greeting, dbus.UInt32(0))

    stream_message, _ = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='MessageSent'),
        )

    # computer says no
    elem = stream_message.stanza
    elem['from'] = 'chat@conf.localhost'
    elem['to'] = 'chat@conf.localhost/test'
    elem['type'] = 'error'
    error = elem.addElement('error')
    if code is not None:
        error['code'] = code
    if type_ is not None:
        error['type'] = type_
    if element is not None:
        error.addElement((ns.STANZA, element))
    if error_message is not None:
        error.addElement((ns.STANZA, 'text')).addContent(error_message)

    stream.send(elem)

    message_received = q.expect('dbus-signal', signal='MessageReceived')

    # Check that the Messages.MessageReceived signal was a failed delivery report
    assertLength(1, message_received.args)
    parts = message_received.args[0]

    if error_message is None:
        # The delivery report should just be a header, no body.
        assertLength(1, parts)
    else:
        assertLength(2, parts)

    part = parts[0]
    # The intended recipient was the MUC, so there's no contact handle
    # suitable for being 'message-sender'.
    assertEquals(0, part.get('message-sender', 0))
    assertEquals(cs.MT_DELIVERY_REPORT, part['message-type'])
    assertEquals(delivery_status, part['delivery-status'])
    assertEquals(send_error_value, part['delivery-error'])
    assertEquals(sent_token, part['delivery-token'])

    # Check that the included echo is from us, and matches all the keys in the
    # message we sent.
    assertContains('delivery-echo', part)
    echo = part['delivery-echo']
    assertLength(len(greeting), echo)
    echo_header = echo[0]
    assertEquals(test_handle, echo_header['message-sender'])
    assertEquals(sent_token, echo_header['message-token'])

    for i in range(0, len(echo)):
        for key in greeting[i]:
            assert key in echo[i], (i, key, echo)
            assert echo[i][key] == greeting[i][key], (i, key, echo, greeting)

    if error_message is not None:
        body = parts[1]

        assertEquals('text/plain', body['content-type'])
        assertEquals(error_message, body['content'])


if __name__ == '__main__':
    exec_test(test)
