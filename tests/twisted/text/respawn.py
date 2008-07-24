"""
Test text channel being recreated because there are still pending messages.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import exec_test
from servicetest import call_async, EventPattern, tp_path_prefix

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()

    jid = 'foo@bar.com'
    call_async(q, conn, 'RequestHandles', 1, [jid])

    event = q.expect('dbus-return', method='RequestHandles')
    foo_handle = event.value[0][0]

    call_async(q, conn, 'RequestChannel',
        'org.freedesktop.Telepathy.Channel.Type.Text', 1, foo_handle, True)

    ret, sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        )

    text_chan = bus.get_object(conn.bus_name, ret.value[0])
    chan_iface = dbus.Interface(text_chan,
            'org.freedesktop.Telepathy.Channel')
    text_iface = dbus.Interface(text_chan,
            'org.freedesktop.Telepathy.Channel.Type.Text')

    assert sig.args[0] == ret.value[0]
    assert sig.args[1] == u'org.freedesktop.Telepathy.Channel.Type.Text'
    # check that handle type == contact handle
    assert sig.args[2] == 1
    assert sig.args[3] == foo_handle
    assert sig.args[4] == True      # suppress handler

    future_props = text_chan.GetAll(
            'org.freedesktop.Telepathy.Channel.FUTURE',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert future_props['Requested'] == True
    assert future_props['TargetID'] == jid,\
            (future_props['TargetID'], jid)
    assert future_props['InitiatorHandle'] == self_handle,\
            (future_props['InitiatorHandle'], self_handle)
    assert future_props['InitiatorID'] == 'test@localhost',\
            future_props['InitiatorID']

    text_iface.Send(0, 'hey')

    event = q.expect('stream-message')

    elem = event.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'chat'
    body = list(event.stanza.elements())[0]
    assert body.name == 'body'
    assert body.children[0] == u'hey'

    # <message type="chat"><body>hello</body</message>
    m = domish.Element(('', 'message'))
    m['from'] = 'foo@bar.com/Pidgin'
    m['type'] = 'chat'
    m.addElement('body', content='hello')
    stream.send(m)

    event = q.expect('dbus-signal', signal='Received')

    hello_message_id = event.args[0]
    hello_message_time = event.args[1]
    assert event.args[2] == foo_handle
    # message type: normal
    assert event.args[3] == 0
    # flags: none
    assert event.args[4] == 0
    # body
    assert event.args[5] == 'hello'

    messages = text_chan.ListPendingMessages(False,
            dbus_interface='org.freedesktop.Telepathy.Channel.Type.Text')
    assert messages == \
            [(hello_message_id, hello_message_time, foo_handle,
                0, 0, 'hello')], messages

    # close the channel without acking the message; it comes back

    call_async(q, chan_iface, 'Close')

    event = q.expect('dbus-signal', signal='Closed')
    assert tp_path_prefix + event.path == text_chan.object_path,\
            (tp_path_prefix + event.path, text_chan.object_path)

    event = q.expect('dbus-signal', signal='NewChannel')
    assert event.args[0] == text_chan.object_path
    assert event.args[1] == u'org.freedesktop.Telepathy.Channel.Type.Text'
    assert event.args[2] == 1   # CONTACT
    assert event.args[3] == foo_handle
    assert event.args[4] == False   # suppress handler

    event = q.expect('dbus-return', method='Close')

    # it now behaves as if the message had initiated it

    future_props = text_chan.GetAll(
            'org.freedesktop.Telepathy.Channel.FUTURE',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert future_props['Requested'] == False
    assert future_props['TargetID'] == jid,\
            (future_props['TargetID'], jid)
    assert future_props['InitiatorHandle'] == foo_handle,\
            (future_props['InitiatorHandle'], foo_handle)
    assert future_props['InitiatorID'] == 'foo@bar.com',\
            future_props['InitiatorID']

    # the message is still there

    messages = text_chan.ListPendingMessages(False,
            dbus_interface='org.freedesktop.Telepathy.Channel.Type.Text')
    assert messages == \
            [(hello_message_id, hello_message_time, foo_handle,
                0, 0, 'hello')], messages

    # acknowledge it

    text_chan.AcknowledgePendingMessages([hello_message_id],
            dbus_interface='org.freedesktop.Telepathy.Channel.Type.Text')

    messages = text_chan.ListPendingMessages(False,
            dbus_interface='org.freedesktop.Telepathy.Channel.Type.Text')
    assert messages == []

    # close the channel again

    call_async(q, chan_iface, 'Close')

    event = q.expect('dbus-signal', signal='Closed')
    assert tp_path_prefix + event.path == text_chan.object_path,\
            (tp_path_prefix + event.path, text_chan.object_path)

    event = q.expect('dbus-return', method='Close')

    # assert that it stays dead this time!

    try:
        chan_iface.GetChannelType()
    except dbus.DBusException:
        pass
    else:
        raise AssertionError("Why won't it die?")

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

