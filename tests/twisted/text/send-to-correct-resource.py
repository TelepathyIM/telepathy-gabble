"""
Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=22369.
"""

from twisted.words.xish import domish

from servicetest import wrap_channel
from gabbletest import exec_test
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # <wjt> I need a random name generator
    # <fledermaus> Macro-Variable Spin Gel
    contact = 'macro-variable.spin.gel@example.com'
    contact_a = '%s/n810' % contact
    contact_b = '%s/laptop' % contact

    path, _ = conn.Requests.CreateChannel({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_ID: contact,
    })
    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text')

    # When we start a conversation, Gabble should send to the bare JID.
    chan.Text.Send(0, 'hey, you around?')
    q.expect('stream-message', to=contact)

    # A particular resource replies.
    m = domish.Element((None, 'message'))
    m['from'] = contact_a
    m['type'] = 'chat'
    m.addElement('body', content="i'm on a beach at Gran Canaria!")
    stream.send(m)

    q.expect('dbus-signal', signal='Received')

    # Now that we got a reply from a particular resource, Gabble should reply
    # there.
    chan.Text.Send(0, 'nice')
    q.expect('stream-message', to=contact_a)

    # Now another resource messages us
    m = domish.Element((None, 'message'))
    m['from'] = contact_b
    m['type'] = 'chat'
    m.addElement('body', content="I brought my laptop to the Empathy hackfest")
    stream.send(m)

    q.expect('dbus-signal', signal='Received')

    # Gabble should have updated the resource it's sending to.
    chan.Text.Send(0, "don't get sand in the keyboard")
    e = q.expect('stream-message', to=contact_b)

    # But actually that resource has gone offline:
    m = e.stanza
    m['from'] = contact_b
    m['type'] = 'error'
    del m['to']

    err = m.addElement((None, 'error'))
    err['type'] = 'cancel'
    err.addElement((ns.STANZA, 'item-not-found'))

    stream.send(m)
    q.expect('dbus-signal', signal='SendError')

    # So as a result, Gabble should send the next message to the bare JID.
    chan.Text.Send(0, "... i guess my warning was too late")
    q.expect('stream-message', to=contact)

if __name__ == '__main__':
    exec_test(test)
