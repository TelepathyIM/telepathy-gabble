"""
Test what happens if there's a timeout in getting vcard request.
"""

import base64
import time

import dbus

from servicetest import call_async, lazy, match, tp_name_prefix, unwrap
from gabbletest import go, handle_get_vcard, handle_set_vcard

from twisted.words.xish import xpath
from twisted.words.xish import domish
from twisted.words.protocols.jabber.client import IQ

def aliasing_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Aliasing')

def avatars_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Avatars')

@lazy
@match('stream-iq')
def expect_get_inital_vcard(event, data):
    return handle_get_vcard(event, data)
    pass

# Make two edits (alias, avatar) and request avatar again.
# Cache should be invalidated, a new GET request made, then
# RequestAvatar should return with the newly returned (NOT patched)
# avatar, and server should receive SET request.

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):

    handle = data['conn_iface'].GetSelfHandle()
    data['self_handle'] = handle

    call_async(data['test'], aliasing_iface(data['conn']),
               'SetAliases', {handle: 'Some Guy'})
    call_async(data['test'], avatars_iface(data['conn']),
               'SetAvatar', 'hello', 'image/png')
    call_async(data['test'], avatars_iface(data['conn']),
                'RequestAvatar', handle)

    return True


# Gabble has invalidate its cache and asks us for our vcard (this
# is for the first edit call, the second will reuse the same reply)
#@match('stream-iq')
#def expect_get_vcard(event, data):
#    if event.stanza['type'] != 'get':
#        return False
#    data['stream'].send(' ')
#    # time.sleep(1) # give gabble chance to process the other Set request...
#    return handle_get_vcard(event, data)

@lazy
@match('stream-iq')
def expect_set_vcard(event, data):
    return handle_set_vcard(event, data)

# We *still* don't have photo in the vCard
@match('dbus-error', method='RequestAvatar')
def expect_avatar_error2(event, data):
    assert event.error.args[0] == 'contact vCard has no photo'
    return True

# Only after we get AvatarUpdated with the new
# token, we should be able to request new avatar
@match('dbus-signal', signal='AvatarUpdated')
def expect_avatar_updated(event, data):
    handle = data['self_handle']
    call_async(data['test'], avatars_iface(data['conn']),
                'RequestAvatar', handle)

    return True

@match('dbus-return', method='RequestAvatar')
def expect_avatar_error3(event, data):
    assert unwrap(event.value)[1] == 'image/png'
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

