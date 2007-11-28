"""Exhibit a bug: RequestChannel D-Bus timeout when requesting a group channel
when the roster wasn't received yet"""

# must come before the twisted imports due to side-effects
from gabbletest import go, make_result_iq
from servicetest import call_async, lazy, match, tp_name_prefix, unwrap, Event

from twisted.internet.protocol import Factory, Protocol
from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish, xpath
from twisted.internet import reactor

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):


    return True

@match('stream-iq', query_ns='jabber:iq:roster')
def expect_roster_iq(event, data):
    event.stanza['type'] = 'result'

    # handle type is Handle_Type_Group
    call_async(data['test'], data['conn_iface'], 'RequestHandles', 4,
            ['test'])

    # We'll send the reply *after* our channel request
    data['roster_reply'] = event.stanza

    return True

@match('dbus-return', method='RequestHandles')
def expect_request_handles_return(event, data):
    handles = event.value[0]

    call_async(data['test'], data['conn_iface'], 'RequestChannel',
    'org.freedesktop.Telepathy.Channel.Type.ContactList', 4, handles[0], True)

    data['stream'].send(data['roster_reply'])

    return True

@match('dbus-return', method='RequestChannel')
def expect_request_channel_return(event, data):
    return True

if __name__ == '__main__':
    go()
