"""
Torture tests for vcard manager, setting and getting of vcards.
"""

import base64
import time

import dbus

from servicetest import call_async, lazy, match, tp_name_prefix, unwrap
from gabbletest import go

from twisted.words.xish import xpath
from twisted.words.xish import domish
from twisted.words.protocols.jabber.client import IQ

def aliasing_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Aliasing')

def avatars_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Avatars')

current_vcard = domish.Element(('vcard-temp', 'vCard'))

def handle_get_vcard(event, data):
    iq = event.stanza

    if iq['type'] != 'get':
        return False

    if iq.uri != 'jabber:client':
        return False

    vcard = list(iq.elements())[0]

    if vcard.name != 'vCard':
        return False

    # Send back current vCard
    new_iq = IQ(data['stream'], 'result')
    new_iq['id'] = iq['id']
    new_iq.addChild(current_vcard)    
    data['stream'].send(new_iq)
    return True

def handle_set_vcard(event, data):
    iq = event.stanza

    if iq['type'] != 'set':
        return False

    if iq.uri != 'jabber:client':
        return False

    vcard = list(iq.elements())[0]

    if vcard.name != 'vCard':
        return False

    current_vcard = vcard
    
    new_iq = IQ(data['stream'], 'result')
    new_iq['id'] = iq['id']
    data['stream'].send(new_iq)
    return True


# Gabble requests vCard immediately upon connecting,
# so this happens before StatusChanged signal

@lazy
@match('stream-iq')
def expect_get_inital_vcard(event, data):
    return handle_get_vcard(event, data)

# Test 1
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

# Test 2
# Make two edits (alias, avatar) and request avatar again.
# Cache should be invalidated, a new GET request made, then
# RequestAvatar should return with the newly returned + patched
# avatar, and server should receive SET request.

# We don't have a vCard yet
@match('dbus-error', method='RequestAvatar')
def expect_avatar_error1(event, data):
    assert event.error.message == 'contact vCard has no photo'
    
    handle = data['self_handle']
    # Test 2 commences
    call_async(data['test'], aliasing_iface(data['conn']),
               'SetAliases', {handle: 'Some Guy'})
    call_async(data['test'], avatars_iface(data['conn']),
               'SetAvatar', 'hello', 'image/png')
    call_async(data['test'], avatars_iface(data['conn']),
                'RequestAvatar', handle)

    return True

# Gabble has invalidate its cache and asks us for our vcard (this
# is for the first edit call, the second will reuse the same reply)
@match('stream-iq')
def expect_get_vcard(event, data):
    return handle_get_vcard(event, data)

# We have a problem here - gabble updates our avatar token when
# the edit callback successfully returns, but our vcard is updated
# long before that, so there's a window in which we have old avatar
# token but new avatar. When user does RequestAvatar() in that window,
# there will be a mismatch. XXX - TODO - FIXME - etc...
@match('dbus-error', method='RequestAvatar')
def expect_avatar_error2(event, data):
    assert event.error.message == 'avatar hash in presence does not match avatar in vCard'

    data['conn_iface'].Disconnect()
    return True


# FIXME - one possible problem with the test is that gabble might
# be too fast for us, so set aliases will immediately get, patch, set vcard,
# before gabble gets setavatar. So if the test hangs, this might be the cause.
    
### TODO - more tests here


@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

