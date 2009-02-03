"""
Test text channel initiated by me, using Requests.EnsureChannel
"""

import dbus

from twisted.words.xish import domish

from gabbletest import exec_test
from servicetest import call_async, EventPattern

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()

    jids = ['foo@bar.com', 'truc@cafe.fr']
    call_async(q, conn, 'RequestHandles', 1, jids)

    event = q.expect('dbus-return', method='RequestHandles')
    handles = event.value[0]

    properties = conn.GetAll(
            'org.freedesktop.Telepathy.Connection.Interface.Requests',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert properties.get('Channels') == [], properties['Channels']
    assert ({'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Text',
             'org.freedesktop.Telepathy.Channel.TargetHandleType': 1,
             },
             ['org.freedesktop.Telepathy.Channel.TargetHandle',
              'org.freedesktop.Telepathy.Channel.TargetID'
             ],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    requestotron = dbus.Interface(conn,
            'org.freedesktop.Telepathy.Connection.Interface.Requests')

    test_ensure_ensure(q, requestotron, conn, self_handle, jids[0], handles[0])
    test_request_ensure(q, requestotron, conn, self_handle, jids[1], handles[1])

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])


def test_ensure_ensure(q, requestotron, conn, self_handle, jid, handle):
    """
    Test ensuring a non-existant channel twice.  The first call should succeed
    with Yours=True; the subsequent call should succeed with Yours=False
    """

    # Check that Ensuring a channel that doesn't exist succeeds
    call_async(q, requestotron, 'EnsureChannel', request_props (handle))

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='EnsureChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 3
    yours, path, emitted_props = ret.value

    # The channel was created in response to the call, and we were the only
    # requestor, so we should get Yours=True
    assert yours, ret.value

    check_props(emitted_props, self_handle, handle, jid)

    assert len(old_sig.args) == 5
    old_path, old_ct, old_ht, old_h, old_sh = old_sig.args

    assert old_path == path
    assert old_ct == u'org.freedesktop.Telepathy.Channel.Type.Text'
    # check that handle type == contact handle
    assert old_ht == 1
    assert old_h == handle
    assert old_sh == True      # suppress handler

    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1        # one channel
    assert len(new_sig.args[0][0]) == 2     # two struct members
    assert new_sig.args[0][0][0] == path
    assert new_sig.args[0][0][1] == emitted_props

    properties = conn.GetAll(
            'org.freedesktop.Telepathy.Connection.Interface.Requests',
            dbus_interface=dbus.PROPERTIES_IFACE)

    assert new_sig.args[0][0] in properties['Channels'], \
            (new_sig.args[0][0], properties['Channels'])


    # Now try Ensuring a channel which already exists
    call_async(q, requestotron, 'EnsureChannel', request_props (handle))
    ret_ = q.expect('dbus-return', method='EnsureChannel')

    assert len(ret_.value) == 3
    yours_, path_, emitted_props_ = ret_.value

    # Someone's already responsible for this channel, so we should get
    # Yours=False
    assert not yours_, ret_.value
    assert path == path_, (path, path_)
    assert emitted_props == emitted_props_, (emitted_props, emitted_props_)


def test_request_ensure(q, requestotron, conn, self_handle, jid, handle):
    """
    Test Creating a non-existant channel, then Ensuring the same channel.
    The call to Ensure should succeed with Yours=False.
    """

    call_async(q, requestotron, 'CreateChannel', request_props (handle))

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 2
    path, emitted_props = ret.value

    check_props(emitted_props, self_handle, handle, jid)

    assert len(old_sig.args) == 5
    old_path, old_ct, old_ht, old_h, old_sh = old_sig.args

    assert old_path == path
    assert old_ct == u'org.freedesktop.Telepathy.Channel.Type.Text'
    # check that handle type == contact handle
    assert old_ht == 1
    assert old_h == handle
    assert old_sh == True      # suppress handler

    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1        # one channel
    assert len(new_sig.args[0][0]) == 2     # two struct members
    assert new_sig.args[0][0][0] == path
    assert new_sig.args[0][0][1] == emitted_props

    properties = conn.GetAll(
            'org.freedesktop.Telepathy.Connection.Interface.Requests',
            dbus_interface=dbus.PROPERTIES_IFACE)

    assert new_sig.args[0][0] in properties['Channels'], \
            (new_sig.args[0][0], properties['Channels'])


    # Now try Ensuring that same channel.
    call_async(q, requestotron, 'EnsureChannel', request_props (handle))
    ret_ = q.expect('dbus-return', method='EnsureChannel')

    assert len(ret_.value) == 3
    yours_, path_, emitted_props_ = ret_.value

    # Someone's already responsible for this channel, so we should get
    # Yours=False
    assert not yours_, ret_.value
    assert path == path_, (path, path_)
    assert emitted_props == emitted_props_, (emitted_props, emitted_props_)


def check_props(props, self_handle, handle, jid):
    assert props['org.freedesktop.Telepathy.Channel.ChannelType'] ==\
            'org.freedesktop.Telepathy.Channel.Type.Text'
    assert props['org.freedesktop.Telepathy.Channel.'
            'TargetHandleType'] == 1
    assert props['org.freedesktop.Telepathy.Channel.TargetHandle'] ==\
            handle
    assert props['org.freedesktop.Telepathy.Channel.TargetID'] == jid
    assert props['org.freedesktop.Telepathy.Channel.'
            'Requested'] == True
    assert props['org.freedesktop.Telepathy.Channel.'
            'InitiatorHandle'] == self_handle
    assert props['org.freedesktop.Telepathy.Channel.'
            'InitiatorID'] == 'test@localhost'


def request_props(handle):
    return { 'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Text',
             'org.freedesktop.Telepathy.Channel.TargetHandleType': 1,
             'org.freedesktop.Telepathy.Channel.TargetHandle': handle,
           }


if __name__ == '__main__':
    exec_test(test)

