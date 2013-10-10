"""
Test that an incoming <message><error/></> for a contact gives
a delivery report on a 1-1 text channel to that contact.
"""

from twisted.words.xish import domish

from gabbletest import exec_test
import constants as cs
import ns

def test_temporary_error(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    jid = 'foo@bar.com'
    foo_handle = conn.get_contact_handle_sync(jid)

    conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: foo_handle,
              })[0]

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

    message_received = q.expect('dbus-signal', signal='MessageReceived')

    expected_send_error = 4 # Too_Long

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
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    jid = 'wee@ninja.jp'
    ninja_handle = conn.get_contact_handle_sync(jid)

    conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: ninja_handle,
              })[0]

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

    message_received = q.expect('dbus-signal', signal='MessageReceived')

    expected_send_error = 2 # Invalid_Contact

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
    test_temporary_error(q, bus, conn, stream)
    test_permanent_error(q, bus, conn, stream)

if __name__ == '__main__':
    exec_test(test)
