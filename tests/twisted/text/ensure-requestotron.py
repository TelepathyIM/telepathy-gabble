"""
Test text channel initiated by me, using Requests.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import exec_test
from servicetest import call_async, EventPattern

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


    # Check that Ensuring a channel that doesn't exist succeeds
    call_async(q, requestotron, 'EnsureChannel',
            { 'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Text',
              'org.freedesktop.Telepathy.Channel.TargetHandleType': 1,
              'org.freedesktop.Telepathy.Channel.TargetHandle': foo_handle,
              })

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

    assert emitted_props['org.freedesktop.Telepathy.Channel.ChannelType'] ==\
            'org.freedesktop.Telepathy.Channel.Type.Text'
    assert emitted_props['org.freedesktop.Telepathy.Channel.'
            'TargetHandleType'] == 1
    assert emitted_props['org.freedesktop.Telepathy.Channel.TargetHandle'] ==\
            foo_handle
    assert emitted_props['org.freedesktop.Telepathy.Channel.TargetID'] == jid
    assert emitted_props['org.freedesktop.Telepathy.Channel.FUTURE.'
            'Requested'] == True
    assert emitted_props['org.freedesktop.Telepathy.Channel.FUTURE.'
            'InitiatorHandle'] == self_handle
    assert emitted_props['org.freedesktop.Telepathy.Channel.FUTURE.'
            'InitiatorID'] == 'test@localhost'

    assert len(old_sig.args) == 5
    old_path, old_ct, old_ht, old_h, old_sh = old_sig.args

    assert old_path == path
    assert old_ct == u'org.freedesktop.Telepathy.Channel.Type.Text'
    # check that handle type == contact handle
    assert old_ht == 1
    assert old_h == foo_handle
    # assert old_sh == True      # suppress handler

    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1        # one channel
    assert len(new_sig.args[0][0]) == 2     # two struct members
    assert new_sig.args[0][0][0] == path
    assert new_sig.args[0][0][1] == emitted_props

    properties = conn.GetAll(
            'org.freedesktop.Telepathy.Connection.Interface.Requests',
            dbus_interface='org.freedesktop.DBus.Properties')

    assert new_sig.args[0][0] in properties['Channels'], \
            (new_sig.args[0][0], properties['Channels'])


    # Now try Ensuring a channel which already exists
    call_async(q, requestotron, 'EnsureChannel',
            { 'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Text',
              'org.freedesktop.Telepathy.Channel.TargetHandleType': 1,
              'org.freedesktop.Telepathy.Channel.TargetHandle': foo_handle,
              })
    ret_ = q.expect('dbus-return', method='EnsureChannel')

    assert len(ret_.value) == 3
    yours_, path_, emitted_props_ = ret_.value

    # Someone's already responsible for this channel, so we should get
    # Yours=False
    assert not yours_, ret_.value
    assert path == path_, (path, path_)
    assert emitted_props == emitted_props_, (emitted_props, emitted_props_)

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

