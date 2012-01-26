"""
Test for fd.o#19930.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import (
    exec_test, make_result_iq, request_muc_handle, wrap_channel, elem,
)
from servicetest import (EventPattern, assertEquals, assertLength,
        assertContains, sync_dbus, call_async)
import constants as cs
import ns

from mucutil import join_muc, echo_muc_presence

def test(q, bus, conn, stream):
    room = 'test@conf.localhost'

    room_handle, chan, path, props, disco = join_muc(q, bus, conn, stream,
            room,
            also_capture=[EventPattern('stream-iq', iq_type='get',
                query_name='query', query_ns=ns.DISCO_INFO, to=room)])

    sync_dbus(bus, q, conn)

    # we call Close...
    call_async(q, chan, 'Close')
    q.expect('dbus-return', method='Close')

    # ...so gabble announces our unavailable presence to the MUC.
    event = q.expect('stream-presence', to=room + '/test')
    elem = event.stanza
    assertEquals('unavailable', elem['type'])

    # while we wait for the conference server to echo our unavailable
    # presence, we try and create the same channel again...
    call_async(q, conn.Requests, 'CreateChannel', {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_ID: room
            })

    # ...which should fail because the channel hasn't closed yet.
    q.expect('dbus-error', method='CreateChannel', name=cs.NOT_AVAILABLE)

    # the conference server finally gets around to echoing our
    # unavailable presence...
    echo_muc_presence(q, stream, elem, 'none', 'participant')

    # ...and only now is the channel closed.
    q.expect_many(EventPattern('dbus-signal', signal='Closed'),
                  EventPattern('dbus-signal', signal='ChannelClosed'))

    # now that the channel has finally closed, let's try and request
    # it again which should succeed!
    _, chan, _, _ = join_muc(q, bus, conn, stream, room)

    # let's clear up though.
    chan.Close()
    event = q.expect('stream-presence', to=room + '/test')
    echo_muc_presence(q, stream, event.stanza, 'none', 'participant')
    q.expect_many(EventPattern('dbus-signal', signal='Closed'),
                  EventPattern('dbus-signal', signal='ChannelClosed'))

def test_then_disconnect(q, bus, conn, stream):
    room = 'test@conf.localhost'

    room_handle, chan, path, props, disco = join_muc(q, bus, conn, stream,
            room,
            also_capture=[EventPattern('stream-iq', iq_type='get',
                query_name='query', query_ns=ns.DISCO_INFO, to=room)])

    sync_dbus(bus, q, conn)

    # we call Close...
    call_async(q, chan, 'Close')
    q.expect('dbus-return', method='Close')

    # ...so gabble announces our unavailable presence to the MUC.
    event = q.expect('stream-presence', to=room + '/test')
    elem = event.stanza
    assertEquals('unavailable', elem['type'])

    # oh no, but now we want to disconnect.
    call_async(q, conn, 'Disconnect')

    # the muc factory is told to close everything, so it does so
    # without announcing it to the channel because it does it
    # forcibly, so the channels disappear.
    q.expect_many(EventPattern('dbus-signal', signal='Closed'),
                  EventPattern('dbus-signal', signal='ChannelClosed'))

    # now echo the unavailable presence; this shouldn't be handled
    # because the channel has already closed.
    echo_muc_presence(q, stream, elem, 'none', 'participant')

    # send the stream footer so that the connection thinks it's
    # property disconnected now.
    stream.sendFooter()

    # finally, Disconnect returns
    q.expect('dbus-return', method='Disconnect')

def test_with_password(q, bus, conn, stream):
    room = 'chat@conf.localhost'
    handle = request_muc_handle(q, conn, stream, room)

    call_async(q, conn.Requests, 'CreateChannel', {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_HANDLE: handle})

    expected_muc_jid = '%s/%s' % (room, 'test')
    q.expect('stream-presence', to=expected_muc_jid)

    # tell gabble the room needs a password
    stream.send(
        elem('jabber:client', 'presence', from_=expected_muc_jid,
            type='error')(
          elem(ns.MUC, 'x'),
          elem('error', type='auth')(
            elem(ns.STANZA, 'not-authorized'),
          ),
        ))

    cc, _ = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='PasswordFlagsChanged',
            args=[cs.PASSWORD_FLAG_PROVIDE, 0]))

    chan = wrap_channel(bus.get_object(conn.bus_name, cc.value[0]), 'Text')

    forbidden = [EventPattern('stream-presence', to=expected_muc_jid)]
    q.forbid_events(forbidden)

    # we call Close...
    call_async(q, chan, 'Close')

    # ...but this time no unavailable presence because we were in the
    # auth state, so the channel closes immediately.
    q.expect_many(EventPattern('dbus-return', method='Close'),
                  EventPattern('dbus-signal', signal='Closed'),
                  EventPattern('dbus-signal', signal='ChannelClosed'))

    q.unforbid_events(forbidden)

if __name__ == '__main__':
    exec_test(test)
    exec_test(test_then_disconnect)
    exec_test(test_with_password)
