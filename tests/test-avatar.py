
"""
Test avatar support.
"""

import base64

import dbus
from servicetest import tp_name_prefix, call_async
from gabbletest import go

def avatars_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Avatars')

def expect_connected(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'StatusChanged':
        return False

    if event.args != [0, 1]:
        return False

    handle = data['conn_iface'].RequestHandles(1, ['bob@foo.com'])[0]
    call_async(data['test'], avatars_iface(data['conn']), 'RequestAvatar',
        handle, byte_arrays=True)
    return True

def expect_vcard_iq(event, data):
    if event.type != 'stream-iq':
        return False

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

def expect_RequestAvatar_return(event, data):
    if event.type != 'dbus-return':
        return False

    if event.method != 'RequestAvatar':
        return False

    assert event.value[0] == 'hello'
    assert event.value[1] == 'image/png'
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

