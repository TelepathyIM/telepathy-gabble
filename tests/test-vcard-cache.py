"""
Tests basic vCard caching functionality.
"""

import base64
import time

import dbus

from servicetest import call_async, lazy, match, tp_name_prefix, unwrap
from gabbletest import go, handle_get_vcard, make_result_iq

from twisted.words.xish import xpath
from twisted.words.xish import domish
from twisted.words.protocols.jabber.client import IQ

def aliasing_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Aliasing')

def avatars_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Avatars')

# Gabble requests vCard immediately upon connecting,
# so this happens before StatusChanged signal

@lazy
@match('stream-iq')
def expect_get_inital_vcard(event, data):
    return handle_get_vcard(event, data)

# Request our alias and avatar, expect them to be
# resolved from cache.

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):

    handle = data['conn_iface'].GetSelfHandle()
    data['self_handle'] = handle

    call_async(data['test'], avatars_iface(data['conn']),
                'RequestAvatar', handle)
    call_async(data['test'], aliasing_iface(data['conn']),
                'RequestAliases', [handle])
    data['returned'] = set()

    return True

@lazy
@match('stream-iq', to='test@localhost', iq_type='get')
def expect_pep_iq(event, data):
    iq = event.stanza
    pubsub = iq.firstChildElement()
    assert pubsub.name == 'pubsub'
    assert pubsub.uri == "http://jabber.org/protocol/pubsub"
    items = pubsub.firstChildElement()
    assert items.name == 'items'
    assert items['node'] == "http://jabber.org/protocol/nick"

    result = make_result_iq(data['stream'], iq)
    result['type'] = 'error'
    error = result.addElement('error')
    error['type'] = 'auth'
    error.addElement('forbidden', 'urn:ietf:params:xml:ns:xmpp-stanzas')
    data['stream'].send(result)
    return True

# Default alias is our username
@lazy
@match('dbus-return', method='RequestAliases')
def expect_aliases_return1(event, data):
    assert unwrap(event.value[0]) == ['test']
    data['returned'].add('RequestAliases')
    if len(data['returned']) == 2:
        data['conn_iface'].Disconnect()
    return True

# FIXME - find out why RequestAliases returns before
# RequestAvatar even though everything's cached
# Probably due to queueing, which means conn-avatars don't
# look into the cache before making a request. Prolly
# should make vcard_request look into the cache itself,
# and return immediately. Or not, if it's g_idle()'d. So
# it's better if conn-aliasing look into the cache itself.

# We don't have a vCard yet
@match('dbus-error', method='RequestAvatar')
def expect_avatar_error1(event, data):
    assert event.error.args[0] == 'contact vCard has no photo'
    data['returned'].add('RequestAvatar')
    if len(data['returned']) == 2:
        data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

