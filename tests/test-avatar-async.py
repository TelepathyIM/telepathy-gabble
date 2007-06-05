
"""
Test support for retrieving avatars asynchronously using RequestAvatars.
"""

import base64
import sha

import dbus

from servicetest import tp_name_prefix, call_async
from gabbletest import go

def conn_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix + '.Connection')

def avatars_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Avatars')

def expect_connected(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    if event[3] != [0, 1]:
        return False

    handle = conn_iface(data['conn']).RequestHandles(1, ['bob@foo.com'])[0]
    data['handle'] = handle
    avatars_iface(data['conn']).RequestAvatars([handle])
    return True

def expect_vcard_iq(event, data):
    if event[0] != 'stream-iq':
        return False

    iq = event[1]

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
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'AvatarRetrieved':
        return False

    assert event[3][0] == data['handle']
    assert event[3][1] == sha.sha('hello').hexdigest()
    assert event[3][2] == 'hello'
    assert event[3][3] == 'image/png'

    # Request again; this request should be satisfied from the avatar cache.
    avatars_iface(data['conn']).RequestAvatars([data['handle']])
    return True

def expect_AvatarRetrieved_again(event, data):
    # This event *must* be the AvatarRetrieved signal.
    assert event[0] == 'dbus-signal'
    assert event[2] == 'AvatarRetrieved'

    assert event[3][0] == data['handle']
    assert event[3][1] == sha.sha('hello').hexdigest()
    assert event[3][2] == 'hello'
    assert event[3][3] == 'image/png'

    conn_iface(data['conn']).Disconnect()
    return True

def expect_disconnected(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    if event[3] != [2, 1]:
        return False

    return True

if __name__ == '__main__':
    go()

