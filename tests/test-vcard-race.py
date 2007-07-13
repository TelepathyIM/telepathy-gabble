"""
Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=11201

 - We try to set our own alias and our own avatar at about the same time
 - SetAliases requests vCard v1
 - SetAvatar requests vCard v1
 - SetAliases receives v1, writes back v2 with new NICKNAME
 - SetAvatar receives v1, writes back v2' with new PHOTO
 - Change to NICKNAME in v2 is lost
"""

import base64

import dbus
from twisted.words.xish import xpath

from servicetest import call_async, lazy, match, tp_name_prefix
from gabbletest import go

def aliasing_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Aliasing')

def avatars_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Avatars')

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    return True

@match('stream-iq')
def expect_get_vcard(event, data):
    # Looking for something like this:
    #   <iq xmlns='jabber:client' type='get' id='262286393608'>
    #      <vCard xmlns='vcard-temp'/>

    iq = event.stanza

    if iq['type'] != 'get':
        return False

    if iq.uri != 'jabber:client':
        return False

    vcard = list(iq.elements())[0]

    if vcard.name != 'vCard':
        return False

    # Send empty vCard back.
    iq['type'] = 'result'
    data['stream'].send(iq)

    call_async(data['test'], data['conn_iface'],
               'GetSelfHandle')
    return True

@match('dbus-return', method='GetSelfHandle')
def expect_got_self_handle(event, data):
    handle = event.value[0]
    call_async(data['test'], aliasing_iface(data['conn']),
               'SetAliases', {handle: 'Some Guy'})
    return True

@match('stream-iq')
def expect_get_vcard_again(event, data):
    # Looking for something like this:
    #   <iq xmlns='jabber:client' type='get' id='262286393608'>
    #      <vCard xmlns='vcard-temp'/>

    iq = event.stanza

    if iq['type'] != 'get':
        return False

    if iq.uri != 'jabber:client':
        return False

    vcard = list(iq.elements())[0]

    if vcard.name != 'vCard':
        return False

    # Send empty vCard back, but only after we've called SetAvatar and it
    # has requested the vCard too (this triggers the race)
    iq['type'] = 'result'
    data['SetAliases-get-reply'] = iq
    call_async(data['test'], avatars_iface(data['conn']),
               'SetAvatar', 'hello', 'image/png')
    return True

@match('stream-iq')
def expect_get_vcard_yet_again(event, data):
    # Looking for something like this:
    #   <iq xmlns='jabber:client' type='get' id='262286393608'>
    #      <vCard xmlns='vcard-temp'/>

    iq = event.stanza

    if iq['type'] != 'get':
        return False

    if iq.uri != 'jabber:client':
        return False

    vcard = list(iq.elements())[0]

    if vcard.name != 'vCard':
        return False

    data['stream'].send(data['SetAliases-get-reply'])

    # Send empty vCard back.
    iq['type'] = 'result'
    data['stream'].send(iq)
    return True

@match('stream-iq')
def expect_set_vcard(event, data):
    iq = event.stanza

    if iq['type'] != 'set':
        return False

    if iq.uri != 'jabber:client':
        return False

    vcard = list(iq.elements())[0]

    if vcard.name != 'vCard':
        return False

    nickname = vcard.firstChildElement()
    assert nickname.name == 'NICKNAME'
    assert str(nickname) == 'Some Guy'
    return True

@match('stream-iq')
def expect_set_vcard_again(event, data):
    iq = event.stanza

    if iq['type'] != 'set':
        return False

    if iq.uri != 'jabber:client':
        return False

    vcard = list(iq.elements())[0]

    if vcard.name != 'vCard':
        return False

    photos = xpath.queryForNodes('/PHOTO', vcard)
    assert photos is not None and len(photos) == 1, repr(photos)
    types = xpath.queryForNodes('/TYPE', photos[0])
    binvals = xpath.queryForNodes('/BINVAL', photos[0])
    assert types is not None and len(types) == 1, repr(types)
    assert binvals is not None and len(binvals) == 1, repr(binvals)
    assert str(types[0]) == 'image/png'
    assert str(binvals[0]) == base64.b64encode('hello')

    nicknames = xpath.queryForNodes('/NICKNAME', vcard)
    assert nicknames is not None
    assert len(nicknames) == 1
    assert str(nicknames[0]) == 'Some Guy'

    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

