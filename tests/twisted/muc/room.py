"""
Test the different ways to request a channel using the Room interface
"""

from gabbletest import exec_test, make_muc_presence
from servicetest import (call_async, EventPattern, assertEquals,
        assertContains)
import constants as cs

import dbus

import re

def create_muc(q, conn, stream, props):
    call_async(q, conn.Requests, 'CreateChannel', props)

    r = q.expect('stream-presence')
    muc_name = r.to.split('/', 2)[0]

    stream.send(make_muc_presence('owner', 'moderator', muc_name, 'test'))

    r = q.expect('dbus-return', method='CreateChannel')

    assertEquals(2, len(r.value))
    return r.value[1]

def test(q, bus, conn, stream):
    conn.Connect()

    # Wait for us to be fully logged in
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])
    q.expect('stream-presence')

    # First create a channel with human-readable name like normal.
    jid = 'booyakasha@conf.localhost'
    props = create_muc(q, conn, stream, {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_ID: jid,
            })

    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_ROOM, props[cs.TARGET_HANDLE_TYPE])
    assertEquals(jid, props[cs.TARGET_ID])
    assertEquals(jid.split('@')[0], props[cs.ROOM_ROOM_ID])
    assertEquals(jid.split('@')[1], props[cs.ROOM_SERVER])

    # Next create a similar human-readable channel but using the new
    # properties.
    jid = 'indahouse@conf.localhost'
    props = create_muc(q, conn, stream, {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.ROOM_ROOM_ID: jid.split('@')[0],
            cs.ROOM_SERVER: jid.split('@')[1],
            })

    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_ROOM, props[cs.TARGET_HANDLE_TYPE])
    assertEquals(jid, props[cs.TARGET_ID])
    assertEquals(jid.split('@')[0], props[cs.ROOM_ROOM_ID])
    assertEquals(jid.split('@')[1], props[cs.ROOM_SERVER])

    # Next create a similar human-readable channel but using the new
    # RoomID property and leave out Server.
    jid = 'indahouse@fallback.conf.localhost'
    props = create_muc(q, conn, stream, {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.ROOM_ROOM_ID: jid.split('@')[0],
            })

    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_ROOM, props[cs.TARGET_HANDLE_TYPE])
    assertEquals(jid, props[cs.TARGET_ID])
    assertEquals(jid.split('@')[0], props[cs.ROOM_ROOM_ID])
    assertEquals(jid.split('@')[1], props[cs.ROOM_SERVER])

    # Now create a uniquely-named channel.
    conf_server = 'conf.localhost'
    props = create_muc(q, conn, stream, {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.ROOM_ROOM_ID: '',
            cs.ROOM_SERVER: conf_server,
            })

    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_ROOM, props[cs.TARGET_HANDLE_TYPE])
    assert re.match(
        r'^private-chat-\w{8}-\w{4}-\w{4}-\w{4}-\w{12}@' + conf_server + '$',
        props[cs.TARGET_ID]), props[cs.TARGET_ID]
    assertEquals('', props[cs.ROOM_ROOM_ID])
    assertEquals(conf_server, props[cs.ROOM_SERVER])

    # Now create a uniquely-named channel with no server.
    conf_server = 'fallback.conf.localhost'
    props = create_muc(q, conn, stream, {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.ROOM_ROOM_ID: '',
            })

    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_ROOM, props[cs.TARGET_HANDLE_TYPE])
    assert re.match(
        r'^private-chat-\w{8}-\w{4}-\w{4}-\w{4}-\w{12}@' + conf_server + '$',
        props[cs.TARGET_ID]), props[cs.TARGET_ID]
    assertEquals('', props[cs.ROOM_ROOM_ID])
    assertEquals(conf_server, props[cs.ROOM_SERVER])

    # Now a channel with non-human-readable name that we set ourselves.
    jid = 'asdf@conf.localhost'
    props = create_muc(q, conn, stream, {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_ID: jid,
            cs.ROOM_ROOM_ID: '',
            cs.ROOM_SERVER: 'conf.localhost',
            })

    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_ROOM, props[cs.TARGET_HANDLE_TYPE])
    assertEquals(jid, props[cs.TARGET_ID])
    assertEquals('', props[cs.ROOM_ROOM_ID])
    assertEquals(jid.split('@')[1], props[cs.ROOM_SERVER])

    # Now a channel with non-human-readable name that we set ourselves
    # and no server property.
    jid = 'hjkl@conf.localhost'
    props = create_muc(q, conn, stream, {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_ID: jid,
            cs.ROOM_ROOM_ID: '',
            })

    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_ROOM, props[cs.TARGET_HANDLE_TYPE])
    assertEquals(jid, props[cs.TARGET_ID])
    assertEquals('', props[cs.ROOM_ROOM_ID])
    assertEquals(jid.split('@')[1], props[cs.ROOM_SERVER])

    # Now a channel with non-human-readable name (with no conf server
    # in the TargetID) that we set ourselves and no server property.
    jid = 'qwerty'
    props = create_muc(q, conn, stream, {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_ID: jid,
            cs.ROOM_ROOM_ID: '',
            })

    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_ROOM, props[cs.TARGET_HANDLE_TYPE])
    assertEquals(jid + '@fallback.conf.localhost', props[cs.TARGET_ID])
    assertEquals('', props[cs.ROOM_ROOM_ID])
    assertEquals('fallback.conf.localhost', props[cs.ROOM_SERVER])

    # Now a channel which already exists (any of the above) with
    # RoomID set to a non-harmful value
    jid = 'booyakasha@conf.localhost'
    call_async(q, conn.Requests, 'EnsureChannel', {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_ID: jid,
            cs.ROOM_ROOM_ID: '',
            })

    q.expect('dbus-return', method='EnsureChannel')

    # Now a channel which already exists (any of the above) with
    # a conflicting Server set.
    jid = 'booyakasha@conf.localhost'
    call_async(q, conn.Requests, 'EnsureChannel', {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_ID: jid,
            cs.ROOM_ROOM_ID: 'happynewyear',
            })

    q.expect('dbus-error', name=cs.INVALID_ARGUMENT, method='EnsureChannel')

    # Now a channel which already exists (any of the above) with
    # a non-conflicting Server set.
    jid = 'booyakasha@conf.localhost'
    call_async(q, conn.Requests, 'EnsureChannel', {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_ID: jid,
            cs.ROOM_SERVER: 'conf.localhost',
            })

    q.expect('dbus-return', method='EnsureChannel')

    # Now a channel which already exists (any of the above) with
    # a conflicting Server set.
    jid = 'booyakasha@conf.localhost'
    call_async(q, conn.Requests, 'EnsureChannel', {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
            cs.TARGET_ID: jid,
            cs.ROOM_SERVER: 'lol.conf.localhost',
            })

    q.expect('dbus-error', name=cs.INVALID_ARGUMENT, method='EnsureChannel')

if __name__ == '__main__':
    exec_test(test, params={ 'fallback-conference-server': 'fallback.conf.localhost' } )
