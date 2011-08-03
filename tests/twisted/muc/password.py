from gabbletest import exec_test, elem, request_muc_handle, make_muc_presence
from servicetest import call_async, EventPattern, wrap_channel, assertEquals
import constants as cs
import ns

def expect_attempt(q, expected_muc_jid, expected_password):
    e = q.expect('stream-presence', to=expected_muc_jid)
    x = e.stanza.elements(uri=ns.MUC, name='x').next()

    p = x.firstChildElement()
    assertEquals(expected_password, str(p))

def test(q, bus, conn, stream):
    room = 'chat@conf.localhost'
    handle = request_muc_handle(q, conn, stream, room)

    call_async(q, conn.Requests, 'CreateChannel', {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_HANDLE: handle})

    expected_muc_jid = '%s/%s' % (room, 'test')
    q.expect('stream-presence', to=expected_muc_jid)

    # tell gabble the room needs a password
    denied = \
        elem('jabber:client', 'presence', from_=expected_muc_jid,
            type='error')(
          elem(ns.MUC, 'x'),
          elem('error', type='auth')(
            elem(ns.STANZA, 'not-authorized'),
          ),
        )
    stream.send(denied)

    cc, _, _ = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        EventPattern('dbus-signal', signal='PasswordFlagsChanged',
            args=[cs.PASSWORD_FLAG_PROVIDE, 0]))

    chan = wrap_channel(bus.get_object(conn.bus_name, cc.value[0]), 'Text',
        ['Password'])

    flags = chan.Password.GetPasswordFlags()
    assertEquals(cs.PASSWORD_FLAG_PROVIDE, flags)

    call_async(q, chan.Password, 'ProvidePassword', 'brand new benz')
    expect_attempt(q, expected_muc_jid, 'brand new benz')

    # Try again while the first attempt is outstanding. Gabble should say no.
    call_async(q, chan.Password, 'ProvidePassword', 'faster faster')
    q.expect('dbus-error', method='ProvidePassword')

    # Sorry, wrong password.
    stream.send(denied)
    ret = q.expect('dbus-return', method='ProvidePassword')
    assert not ret.value[0]

    call_async(q, chan.Password, 'ProvidePassword', 'bougie friends')
    expect_attempt(q, expected_muc_jid, 'bougie friends')

    # Well, this may be the right password, but actually that nick is in use.
    presence = elem('presence', from_=expected_muc_jid, type='error')(
        elem(ns.MUC, 'x'),
        elem('error', type='cancel')(
          elem(ns.STANZA, 'conflict'),
        ))
    stream.send(presence)

    # Okay, so Gabble tries again, with a new JID *and the same password*.
    expected_muc_jid = expected_muc_jid + '_'
    expect_attempt(q, expected_muc_jid, 'bougie friends')

    # Hey this worked.
    stream.send(make_muc_presence('none', 'participant', room, 'test_'))
    ret, _ = q.expect_many(
        EventPattern('dbus-return', method='ProvidePassword'),
        EventPattern('dbus-signal', signal='PasswordFlagsChanged',
            args=[0, cs.PASSWORD_FLAG_PROVIDE]))
    assert ret.value[0]

if __name__ == '__main__':
    exec_test(test)
