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

def test_create_pmuc(q, conn, stream, extra_props=None):
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

    assert re.match(r'^private-chat-\w{8}-\w{4}-\w{4}-\w{4}-\w{12}@groupchat.google.com$', pmuc_name)

    stream.send(make_muc_presence('owner', 'moderator', pmuc_name, 'test'))

    # wait for the method return
    r = q.expect('dbus-return', method='CreateChannel')

    assert len(r.value) == 2
    path, props = r.value

    assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT
    assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_ROOM
    assert props[cs.TARGET_ID] == pmuc_name

    assert CONFERENCE in props[cs.INTERFACES]
    assert props[CONFERENCE + '.InitialChannels'] == []
    assert props[CONFERENCE + '.InitialInviteeIDs'] == []
    assert props[CONFERENCE + '.InitialInviteeHandles'] == []
    assert props[CONFERENCE + '.SupportsNonMerges'] == True

    return pmuc_name, path

if __name__ == '__main__':
    exec_test(test)
