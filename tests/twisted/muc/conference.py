"""
Test the different ways to request a channel using the Conference interface
"""

from gabbletest import exec_test, make_muc_presence
from servicetest import call_async, EventPattern
import constants as cs

import dbus

import re

CONFERENCE = 'org.freedesktop.Telepathy.Channel.Interface.Conference.DRAFT'

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    test_create_pmuc(q, conn, stream)
    test_create_pmuc_with_invitee(q, conn, stream)

def create_pmuc(q, conn, stream, extra_props=None):
    """
    Request a PMUC just for ourselves.
    """

    props = {
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_NONE,
        CONFERENCE + '.InitialChannels': dbus.Array([], signature='o'),
    }

    if extra_props: props.update(extra_props)

    call_async(q, conn.Requests, 'CreateChannel', props)

    # wait for the MUC name, so we can inject a reply
    r = q.expect('stream-presence')
    pmuc_name = r.to.split('/', 2)[0]

    assert re.match(
        r'^private-chat-\w{8}-\w{4}-\w{4}-\w{4}-\w{12}@conf.localhost$',
        pmuc_name), pmuc_name

    stream.send(make_muc_presence('owner', 'moderator', pmuc_name, 'test'))

    # wait for the method return
    r = q.expect('dbus-return', method='CreateChannel')

    assert len(r.value) == 2
    path, out_props = r.value

    assert out_props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT
    assert out_props[cs.TARGET_HANDLE_TYPE] == cs.HT_ROOM
    assert out_props[cs.TARGET_ID] == pmuc_name

    assert CONFERENCE in out_props[cs.INTERFACES]
    assert out_props[CONFERENCE + '.InitialChannels'] == \
            props[CONFERENCE + '.InitialChannels']
    assert out_props[CONFERENCE + '.SupportsNonMerges'] == True

    return pmuc_name, path, out_props

def test_create_pmuc(q, conn, stream):

    pmuc_name, path, props = create_pmuc(q, conn, stream)

    assert props[CONFERENCE + '.InitialInviteeIDs'] == []
    assert props[CONFERENCE + '.InitialInviteeHandles'] == []

def test_create_pmuc_with_invitee(q, conn, stream):

    # Open an initial 1-to-1 connection
    props = {
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_ID: 'bob@localhost',
    }

    call_async(q, conn.Requests, 'EnsureChannel', props)
    r = q.expect('dbus-return', method='EnsureChannel')

    assert len(r.value) == 3
    yours, path, props = r.value

    pmuc_name, path, props = create_pmuc(q, conn, stream, {
        CONFERENCE + '.InitialChannels': dbus.Array([path], signature='o'),
    })

    # FIXME: check for stream-message containing invite for Bob

    assert props[CONFERENCE + '.InitialInviteeIDs'] == \
            ['bob@localhost']

if __name__ == '__main__':
    exec_test(test, params={ 'fallback-conference-server': 'conf.localhost' } )
