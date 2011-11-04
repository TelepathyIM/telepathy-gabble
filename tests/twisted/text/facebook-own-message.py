"""
Tests exposing Facebook's own-message extension via delivery reports.

I would say that this has been reverse-engineered, but reading the completely
trivial protocol out of debug logs is hardly reverse-engineering. It isn't
documented anywhere I can find, mind you.
"""
from servicetest import (
    assertEquals, assertLength, assertContains, wrap_channel, EventPattern,
    sync_dbus,
    )
from gabbletest import exec_test, elem, elem_iq
import constants as cs

NS_FACEBOOK_MESSAGES = "http://www.facebook.com/xmpp/messages"

def test(q, bus, conn, stream):
    def send_own_message(to, text):
        iq = elem_iq(stream, 'set', from_='chat.facebook.com')(
              elem(NS_FACEBOOK_MESSAGES, 'own-message', to=to, self='false')(
                elem('body')(text)
              )
            )
        stream.send(iq)
        q.expect('stream-iq', iq_type='result', iq_id=iq['id'])

    # First, test receiving an own-message stanza for a message sent to a
    # contact we have an open channel for.
    jid = '-5678@chat.facebook.com'
    _, path, props = conn.Requests.EnsureChannel({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_ID: jid,
    })
    channel = wrap_channel(bus.get_object(conn.bus_name, path),
        'Text', ['Messages'])
    handle = props[cs.TARGET_HANDLE]

    text = u'omg omg its ur birthdayy <3 <3 xoxoxoxo'
    send_own_message(to=jid, text=text)
    e = q.expect('dbus-signal', signal='MessageReceived')
    message, = e.args
    assertLength(1, message)
    header = message[0]

    assertEquals(handle, header['message-sender'])
    assertEquals(cs.MT_DELIVERY_REPORT, header['message-type'])
    assertEquals(cs.DELIVERY_STATUS_ACCEPTED, header['delivery-status'])

    assertContains('delivery-echo', header)
    echo = header['delivery-echo']
    echo_header, echo_body = echo

    assertEquals(conn.GetSelfHandle(), echo_header['message-sender'])
    assertEquals('text/plain', echo_body['content-type'])
    assertEquals(text, echo_body['content'])

    channel.Text.AcknowledgePendingMessages([header['pending-message-id']])
    channel.Close()

    # Now test receiving an own-message stanza for a message sent to a contact
    # we don't have a channel open for. It should be ignored (but acked). This
    # is consistent with delivery failure reports.
    q.forbid_events([EventPattern('dbus-signal', signal='MessageReceived')])
    send_own_message(to='-393939@chat.facebook.com',
        text=u'please ignore this message')
    sync_dbus(bus, q, conn)

if __name__ == '__main__':
    exec_test(test, params={'account': 'test@chat.facebook.com'})
