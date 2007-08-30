"""
Tests basic vCard caching functionality.
"""

import base64
import time

import dbus

from servicetest import call_async, lazy, match, tp_name_prefix, unwrap
from gabbletest import go, handle_get_vcard

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

    return True

# FIXME - find out why RequestAliases returns before
# RequestAvatar even though everything's cached
# Probably due to queueing, which means conn-avatars don't
# look into the cache before making a request. Prolly
# should make vcard_request look into the cache itself,
# and return immediately. Or not, if it's g_idle()'d. So
# it's better if conn-aliasing look into the cache itself.

# Default alias is our username
@lazy
@match('dbus-return', method='RequestAliases')
def expect_aliases_return1(event, data):
    assert unwrap(event.value[0]) == ['test']
    return True

# We don't have a vCard yet
@match('dbus-error', method='RequestAvatar')
def expect_avatar_error1(event, data):
    assert event.error.message == 'contact vCard has no photo'
    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

