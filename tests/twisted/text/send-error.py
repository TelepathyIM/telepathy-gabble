"""
Test that an incoming <message><error/></> for a contact gives both a SendError
and a delivery report on a 1-1 text channel to that contact.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import exec_test
from servicetest import call_async, EventPattern

import ns

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()

    jid = 'foo@bar.com'
    call_async(q, conn, 'RequestHandles', 1, [jid])

    event = q.expect('dbus-return', method='RequestHandles')
    foo_handle = event.value[0][0]

    properties = conn.GetAll(
            'org.freedesktop.Telepathy.Connection.Interface.Requests',
            dbus_interface='org.freedesktop.DBus.Properties')

    requestotron = dbus.Interface(conn,
            'org.freedesktop.Telepathy.Connection.Interface.Requests')
    call_async(q, requestotron, 'CreateChannel',
            { 'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Text',
              'org.freedesktop.Telepathy.Channel.TargetHandleType': 1,
              'org.freedesktop.Telepathy.Channel.TargetHandle': foo_handle,
              })

    ret = q.expect('dbus-return', method='CreateChannel')
    text_chan = bus.get_object(conn.bus_name, ret.value[0])

    # <message from='foo@bar.com' type='error'>
    #   <body>what is up, my good sir?</body>
    #   <error code='404' type='cancel'>
    #     <item-not-found xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'/>
    #   </error>
    # </message>
    message_body = 'what is up, my good sir?'

    m = domish.Element(('', 'message'))
    m['from'] = 'foo@bar.com'
    m['type'] = 'error'
    m.addElement('body', content=message_body)

    e = domish.Element(('', 'error'))
    e['code'] = '404'
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

    assert received.args[2] == foo_handle, (received.args, foo_handle)
    assert received.args[3] == 4, received.args # Channel_Text_Message_Type_Delivery_Report
    assert received.args[4] == 2, received.args # Channel_Text_Message_Flag_Non_Text_Content
    assert received.args[5] == '', received.args

    delivery_report = message_received.args[0]
    assert len(delivery_report) == 1, delivery_report
    header = delivery_report[0]
    assert header['message-sender'] == foo_handle, header
    assert header['message-type'] == 4, header # Channel_Text_Message_Type_Delivery_Report
    assert header['delivery-status'] == 3, header # Delivery_Status_Permanently_Failed
    assert 'delivery-token' not in header, header
    assert header['delivery-error'] == expected_send_error, header

    delivery_echo = header['delivery-echo']
    assert len(delivery_echo) == 2, delivery_echo

    assert delivery_echo[0]['message-sender'] == self_handle, delivery_echo
    assert 'message-token' not in delivery_echo[0], delivery_echo
    # FIXME: see above
    #assert delivery_echo[0]['message-type'] == 0, delivery_echo

    assert delivery_echo[1]['content-type'] == "text/plain", delivery_echo
    assert delivery_echo[1]['content'] == message_body, delivery_echo

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

