"""test OLPC bundle. We shouldn't announce OLPC features until we use the OLPC
interface"""
import base64
import errno
import os

import dbus

# must come before the twisted imports due to side-effects
from gabbletest import go, make_result_iq
from servicetest import call_async, lazy, match, tp_name_prefix, unwrap, Event

from twisted.internet.protocol import Factory, Protocol
from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish, xpath
from twisted.internet import reactor

NS_OLPC_BUDDY_PROPS_NOTIFY = "http://laptop.org/xmpp/buddy-properties+notify"
NS_OLPC_ACTIVITIES_NOTIFY = "http://laptop.org/xmpp/activities+notify"
NS_OLPC_CURRENT_ACTIVITY_NOTIFY = "http://laptop.org/xmpp/current-activity+notify"
NS_OLPC_ACTIVITY_PROPS_NOTIFY = "http://laptop.org/xmpp/activity-properties+notify"

olpc_features = set([NS_OLPC_BUDDY_PROPS_NOTIFY, NS_OLPC_ACTIVITIES_NOTIFY,
        NS_OLPC_CURRENT_ACTIVITY_NOTIFY, NS_OLPC_ACTIVITY_PROPS_NOTIFY])

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):

    # send diso request
    m = domish.Element(('', 'iq'))
    m['from'] = 'alice@jabber.laptop.org'
    m['id'] = '1'
    query = m.addElement('query')
    query['xmlns'] = 'http://jabber.org/protocol/disco#info'
    data['stream'].send(m)

    return True

@match('stream-iq', iq_type='result',
    query_ns='http://jabber.org/protocol/disco#info',
    to='alice@jabber.laptop.org')
def expect_first_disco_reply(event, data):
    if event.stanza['id'] != '1':
        return False

    features = set([str(f['var']) for f in xpath.queryForNodes('/iq/query/feature',
        event.stanza)])

    # OLPC NS aren't announced
    assert len(olpc_features.intersection(features)) == 0

    # Use OLPC interface
    data['olpc_iface'] = dbus.Interface(data['conn'],
            'org.laptop.Telepathy.BuddyInfo')
    call_async(data['test'], data['olpc_iface'], 'SetProperties',
            {'color': '#ff0000,#0000ff'})

    return True

@match('stream-presence')
def expect_bundle_presence(event, data):

    c_nodes = xpath.queryForNodes('/presence/c', event.stanza)
    assert c_nodes is not None
    assert len(c_nodes) == 1

    c_node = c_nodes[0]
    assert c_node['ext'] == 'olpc'

    # send diso request
    m = domish.Element(('', 'iq'))
    m['from'] = 'alice@jabber.laptop.org'
    m['id'] = '2'
    query = m.addElement('query')
    query['xmlns'] = 'http://jabber.org/protocol/disco#info'
    data['stream'].send(m)

    return True

@match('stream-iq', iq_type='result',
    query_ns='http://jabber.org/protocol/disco#info',
    to='alice@jabber.laptop.org')
def expect_snd_disco_reply(event, data):
    if event.stanza['id'] != '2':
        return False

    # OLPC NS are now announced
    features = set([str(f['var']) for f in xpath.queryForNodes('/iq/query/feature',
        event.stanza)])

    assert olpc_features.issubset(features)

    return True

if __name__ == '__main__':
    go()
