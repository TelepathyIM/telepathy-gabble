"""
test OLPC Buddy properties change notifications
"""
# FIXME: merge this file to other tests ?

from servicetest import EventPattern
from gabbletest import exec_test, acknowledge_iq
import constants as cs
from util import send_buddy_changed_properties_msg
import ns

def test(q, bus, conn, stream):
    iq_event, disco_event = q.expect_many(
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)

    handles = {}

    handles['alice'] = conn.get_contact_handle_sync('alice@localhost')

    # Alice, one our friends changed her properties
    send_buddy_changed_properties_msg(stream, 'alice@localhost',
            {'color': ('str', '#005FE4,#00A0FF')})

    event = q.expect('dbus-signal', signal='PropertiesChanged',
            args=[handles['alice'], {'color' : '#005FE4,#00A0FF'}])

if __name__ == '__main__':
    exec_test(test)
