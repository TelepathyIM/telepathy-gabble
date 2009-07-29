"""
Test that an incoming <message><error/></> for a contact gives both a SendError
and a delivery report on a 1-1 text channel to that contact.
"""

from twisted.words.xish import domish

from gabbletest import exec_test
from servicetest import call_async, EventPattern
import constants as cs
import ns

def test_temporary_error(q, bus, conn, stream):
    self_handle = conn.GetSelfHandle()

    jid = 'foo@bar.com'
    call_async(q, conn, 'RequestHandles', 1, [jid])

    event = q.expect('dbus-return', method='RequestHandles')
    foo_handle = event.value[0][0]

    path = conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: foo_handle,
              })[0]
    text_chan = bus.get_object(conn.bus_name, path)

    # <message from='foo@bar.com' type='error'>
    #   <body>what is up, my good sir?</body>
    #   <error type='wait'>
    #     <resource-constraint xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'/>
    #   </error>
    # </message>
    message_body = 'what is up, my good sir?'

    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com'
    m['id'] = '1845a1a9-f7bc-4a2e-a885-633aadc81e1b'
    m['type'] = 'error'
    m.addElement('body', content=message_body)

    e = domish.Element((None, 'error'))
    e['type'] = 'wait'
    e.addElement((ns.STANZA, 'resource-constraint'))

    m.addChild(e)

    stream.send(m)

    send_error, received, message_received = q.expect_many(
        EventPattern('dbus-signal', signal='SendError'),
        EventPattern('dbus-signal', signal='Received'),
        EventPattern('dbus-signal', signal='MessageReceived'),
        )

    expected_send_error = 4 # Too_Long

    assert send_error.args[0] == expected_send_error, send_error.args
    # FIXME: It doesn't look like it's possible to know what the original
    # message type is, given that the type attribute of <message> is 'error'
    # for error reports.
    #assert send_error.args[2] == 0, send_error.args
    assert send_error.args[3] == message_body, send_error.args

    assert received.args[2] == foo_handle, (received.args, foo_handle)
    assert received.args[3] == 4, received.args # Channel_Text_Message_Type_Delivery_Report
    assert received.args[4] == 2, received.args # Channel_Text_Message_Flag_Non_Text_Content
    assert received.args[5] == '', received.args

    delivery_report = message_received.args[0]
    assert len(delivery_report) == 1, delivery_report
    header = delivery_report[0]
    assert header['message-sender'] == foo_handle, header
    assert header['message-type'] == 4, header # Channel_Text_Message_Type_Delivery_Report
    assert header['delivery-status'] == 2, header # Delivery_Status_Temporarily_Failed
    assert header['delivery-token'] == '1845a1a9-f7bc-4a2e-a885-633aadc81e1b',\
            header
    assert header['delivery-error'] == expected_send_error, header

    delivery_echo = header['delivery-echo']
    assert len(delivery_echo) == 2, delivery_echo

    assert delivery_echo[0]['message-sender'] == self_handle, delivery_echo
    assert delivery_echo[0]['message-token'] == \
            '1845a1a9-f7bc-4a2e-a885-633aadc81e1b', delivery_echo
    # FIXME: see above
    #assert delivery_echo[0]['message-type'] == 0, delivery_echo

    assert delivery_echo[1]['content-type'] == "text/plain", delivery_echo
    assert delivery_echo[1]['content'] == message_body, delivery_echo


def test_permanent_error(q, bus, conn, stream):
    self_handle = conn.GetSelfHandle()

    jid = 'wee@ninja.jp'
    call_async(q, conn, 'RequestHandles', 1, [jid])

    event = q.expect('dbus-return', method='RequestHandles')
    ninja_handle = event.value[0][0]

    path = conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: ninja_handle,
              })[0]
    text_chan = bus.get_object(conn.bus_name, path)

    # <message from='wee@ninja.jp' type='error'>
    #   <body>hello? is there anyone there?</body>
    #   <error type='cancel'>
    #     <item-not-found xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'/>
    #   </error>
    # </message>
    message_body = 'hello? is there anyone there?'

    m = domish.Element((None, 'message'))
    m['from'] = 'wee@ninja.jp'
    m['type'] = 'error'
    m.addElement('body', content=message_body)

    e = domish.Element((None, 'error'))
    e['type'] = 'cancel'
    e.addElement((ns.STANZA, 'item-not-found'))

    m.addChild(e)

    stream.send(m)

    send_error, received, message_received = q.expect_many(
        EventPattern('dbus-signal', signal='SendError'),
        EventPattern('dbus-signal', signal='Received'),
        EventPattern('dbus-signal', signal='MessageReceived'),
        )

    expected_send_error = 2 # Invalid_Contact

    assert send_error.args[0] == expected_send_error, send_error.args
    # FIXME: It doesn't look like it's possible to know what the original
    # message type is, given that the type attribute of <message> is 'error'
    # for error reports.
    #assert send_error.args[2] == 0, send_error.args
    assert send_error.args[3] == message_body, send_error.args

    assert received.args[2] == ninja_handle, (received.args, ninja_handle)
    assert received.args[3] == 4, received.args # Channel_Text_Message_Type_Delivery_Report
    assert received.args[4] == 2, received.args # Channel_Text_Message_Flag_Non_Text_Content
    assert received.args[5] == '', received.args

    delivery_report = message_received.args[0]
    assert len(delivery_report) == 1, delivery_report
    header = delivery_report[0]
    assert header['message-sender'] == ninja_handle, header
    assert header['message-type'] == 4, header # Channel_Text_Message_Type_Delivery_Report
    assert header['delivery-status'] == 3, header # Delivery_Status_Permanently_Failed
    # the error has no ID, therefore its Telepathy rendition has no
    # delivery-token
    assert 'delivery-token' not in header, header
    assert header['delivery-error'] == expected_send_error, header

    delivery_echo = header['delivery-echo']
    assert len(delivery_echo) == 2, delivery_echo

    assert delivery_echo[0]['message-sender'] == self_handle, delivery_echo
    # the error has no ID, therefore the echo's Telepathy rendition has no
    # message-token
    assert 'message-token' not in delivery_echo[0], delivery_echo
    # FIXME: see above
    #assert delivery_echo[0]['message-type'] == 0, delivery_echo

    assert delivery_echo[1]['content-type'] == "text/plain", delivery_echo
    assert delivery_echo[1]['content'] == message_body, delivery_echo

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    test_temporary_error(q, bus, conn, stream)
    test_permanent_error(q, bus, conn, stream)

if __name__ == '__main__':
    exec_test(test)
