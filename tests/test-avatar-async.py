
"""
Test support for retrieving avatars asynchronously using RequestAvatars.
"""

import base64
import sha

import dbus

from servicetest import tp_name_prefix, match, lazy
from gabbletest import go

def avatars_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Avatars')

@lazy
def expect_connected(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'StatusChanged':
        return False

    if event.args != [0, 1]:
        return False

    handle = data['conn_iface'].RequestHandles(1, ['bob@foo.com'])[0]
    data['handle'] = handle
    avatars_iface(data['conn']).RequestAvatars([handle])
    return True

@match('stream-iq')
def expect_my_vcard_iq(event, data):
    iq = event.stanza
    if iq.getAttribute('to') is not None:
        return False

    vcard = list(iq.elements())[0]

    if vcard.name != 'vCard':
        return False

    iq['type'] = 'result'
    data['stream'].send(iq)
    return True

@match('stream-iq')
def expect_vcard_iq(event, data):
    iq = event.stanza

    if iq.getAttribute('to') != 'bob@foo.com':
        return False

    vcard = list(iq.elements())[0]

    if vcard.name != 'vCard':
        return False

    photo = vcard.addElement('PHOTO')
    photo.addElement('TYPE', content='image/png')
    photo.addElement('BINVAL', content=base64.b64encode('hello'))

    iq['type'] = 'result'
    data['stream'].send(iq)
    return True

def expect_AvatarRetrieved(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'AvatarRetrieved':
        return False

    assert event.args[0] == data['handle']
    assert event.args[1] == sha.sha('hello').hexdigest()
    assert event.args[2] == 'hello'
    assert event.args[3] == 'image/png'

    # Request again; this request should be satisfied from the avatar cache.
    avatars_iface(data['conn']).RequestAvatars([data['handle']])
    return True

def expect_AvatarRetrieved_again(event, data):
    # This event *must* be the AvatarRetrieved signal.
    assert event.type == 'dbus-signal'
    assert event.signal == 'AvatarRetrieved'

    assert event.args[0] == data['handle']
    assert event.args[1] == sha.sha('hello').hexdigest()
    assert event.args[2] == 'hello'
    assert event.args[3] == 'image/png'

    data['conn_iface'].Disconnect()
    return True

def expect_disconnected(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'StatusChanged':
        return False

    if event.args != [2, 1]:
        return False

    return True

if __name__ == '__main__':
    go()

