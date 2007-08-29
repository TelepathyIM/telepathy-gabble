
"""
Test avatar support.
"""

import base64

import dbus
from servicetest import tp_name_prefix, call_async, match, lazy
from gabbletest import go

def avatars_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Avatars')

@lazy
@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    handle = data['conn_iface'].RequestHandles(1, ['bob@foo.com'])[0]
    call_async(data['test'], avatars_iface(data['conn']), 'RequestAvatar',
        handle, byte_arrays=True)
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

@match('dbus-return', method='RequestAvatar')
def expect_RequestAvatar_return(event, data):
    assert event.value[0] == 'hello'
    assert event.value[1] == 'image/png'
    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

