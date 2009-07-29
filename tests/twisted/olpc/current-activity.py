"""
test OLPC Buddy properties current activity
"""

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, acknowledge_iq
import constants as cs
from util import (announce_gadget, send_buddy_changed_current_act_msg,
    answer_to_current_act_pubsub_request, answer_error_to_pubsub_request,
    send_gadget_current_activity_changed_msg, request_random_activity_view)
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)
    announce_gadget(q, stream, disco_event.stanza)

    buddy_info_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    gadget_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Gadget')

    handles = {}

    # Alice is one of our friend so we receive her PEP notifications
    handles['alice'] = conn.RequestHandles(1, ['alice@localhost'])[0]

    # Try to get Alice's currrent-activity
    call_async(q, buddy_info_iface, "GetCurrentActivity", handles['alice'])

    # Alice's current-activity is not in the cache so Gabble sends a PEP query
    event = q.expect('stream-iq', iq_type='get', query_name='pubsub')
    answer_to_current_act_pubsub_request(stream, event.stanza, 'activity1',
        'room1@conference.localhost')

    event = q.expect('dbus-return', method='GetCurrentActivity')
    id, handles['room1'] = event.value
    assert id == 'activity1'
    assert conn.InspectHandles(2, [handles['room1']]) == \
            ['room1@conference.localhost']

    # Retry to get Alice's current-activity
    # Alice's current-activity is now in the cache so Gabble doesn't
    # send PEP query
    assert buddy_info_iface.GetCurrentActivity(handles['alice']) == \
            ('activity1', handles['room1'])

    # Alice changed her current-activity
    send_buddy_changed_current_act_msg(stream, 'alice@localhost', 'activity2',
            'room2@conference.localhost')

    event = q.expect('dbus-signal', signal='CurrentActivityChanged')
    contact, id, handles['room2'] = event.args
    assert contact == handles['alice']
    assert id == 'activity2'
    assert conn.InspectHandles(2, [handles['room2']]) == \
            ['room2@conference.localhost']

    # Get Alice's current-activity as the cache have to be updated
    assert buddy_info_iface.GetCurrentActivity(handles['alice']) == \
            ('activity2', handles['room2'])

    # request a activity view containing only Bob and one
    # activity in it.
    request_random_activity_view(q, stream, conn, 1, '1',
        [('activity3', 'room3@conference.localhost', {},
            [('bob@localhost', {}),]),])

    # Gadget sends us a current-activity change concerning a
    # known activity
    send_gadget_current_activity_changed_msg(stream, 'bob@localhost', '1',
        'activity3', 'room3@conference.localhost')

    # Gadget notifies us about the change
    event = q.expect('dbus-signal', signal='CurrentActivityChanged')
    handles['bob'], id, handles['room3'] = event.args
    assert id == 'activity3'
    assert conn.InspectHandles(1, [handles['bob']]) == \
            ['bob@localhost']
    assert conn.InspectHandles(2, [handles['room3']]) == \
            ['room3@conference.localhost']

    # And the cache was properly updated
    assert buddy_info_iface.GetCurrentActivity(handles['bob']) == \
            ('activity3', handles['room3'])

    # Gadget sends us a current-activity change concerning an
    # unknown activity
    send_gadget_current_activity_changed_msg(stream, 'bob@localhost', '1',
        'activity4', 'room4@conference.localhost')

    # Gadget changed Alice's current-activity to none as it doesn't
    # know the activity
    event = q.expect('dbus-signal', signal='CurrentActivityChanged',
            args=[handles['bob'], '', 0])

    call_async(q, buddy_info_iface, "GetCurrentActivity", handles['bob'])

    # Bob's current-activity is not in the cache anymore so Gabble try
    # to send a PEP query
    event = q.expect('stream-iq', iq_type='get', query_name='pubsub')
    iq = event.stanza

    # Alice is not Bob's friend so she can't query his PEP node
    answer_error_to_pubsub_request(stream, iq)

    # so Bob is considererd without current activity
    q.expect('dbus-return', method='GetCurrentActivity',
            value=('', 0))

if __name__ == '__main__':
    exec_test(test)
