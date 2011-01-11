"""
fd.o#25533 demonstrated that if you requested a {dbus,stream} tube
channel to a room before you had actually joined the room
(i.e. GabbleMucChannel was not ready yet) then the
{Create,Request,Ensure}Channel return would be the Tubes channel
(there for compatibility reasons), not the Tube channel you
requested.
"""

import dbus

from servicetest import call_async, EventPattern, assertContains, assertEquals
from gabbletest import exec_test, acknowledge_iq, elem, make_muc_presence

import constants as cs
import tubetestutil as t
from mucutil import join_muc

from twisted.words.xish import xpath

def test(q, bus, conn, stream):
    iq_event = q.expect('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard')

    acknowledge_iq(stream, iq_event.stanza)
    t.check_conn_properties(q, conn)

    # Create new style tube channel and make sure that is indeed
    # returned.
    muc = 'chat@conf.localhost'

    call_async(q, conn.Requests, 'CreateChannel',
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
          cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
          cs.TARGET_ID: muc,
          cs.DBUS_TUBE_SERVICE_NAME: 'com.example.LolDongs'})

    q.expect('stream-presence', to='%s/test' % muc)
    stream.send(make_muc_presence('owner', 'moderator', muc, 'bob'))
    stream.send(make_muc_presence('none', 'participant', muc, 'test'))

    ret, _, _ = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    _, props = ret.value

    assertEquals(props[cs.CHANNEL_TYPE], cs.CHANNEL_TYPE_DBUS_TUBE)
    assertEquals(props[cs.TARGET_HANDLE_TYPE], cs.HT_ROOM)
    assertEquals(props[cs.TARGET_ID], muc)

    # Now try joining the text muc before asking for the tube channel.
    muc = 'chat2@conf.localhost'

    join_muc(q, bus, conn, stream, muc)

    call_async(q, conn.Requests, 'CreateChannel',
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
          cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
          cs.TARGET_ID: muc,
          cs.DBUS_TUBE_SERVICE_NAME: 'com.example.LolDongs'})

    ret, _, _ = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    _, props = ret.value

    assertEquals(props[cs.CHANNEL_TYPE], cs.CHANNEL_TYPE_DBUS_TUBE)
    assertEquals(props[cs.TARGET_HANDLE_TYPE], cs.HT_ROOM)
    assertEquals(props[cs.TARGET_ID], muc)

    # Now make sure we can get our Tubes channel if we request it.
    muc = 'chat3@conf.localhost'

    call_async(q, conn.Requests, 'CreateChannel',
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TUBES,
          cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
          cs.TARGET_ID: muc})

    q.expect('stream-presence', to='%s/test' % muc)
    stream.send(make_muc_presence('owner', 'moderator', muc, 'bob'))
    stream.send(make_muc_presence('none', 'participant', muc, 'test'))

    ret, _, _ = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    _, props = ret.value

    assertEquals(props[cs.CHANNEL_TYPE], cs.CHANNEL_TYPE_TUBES)
    assertEquals(props[cs.TARGET_HANDLE_TYPE], cs.HT_ROOM)
    assertEquals(props[cs.TARGET_ID], muc)

if __name__ == '__main__':
    exec_test(test)
