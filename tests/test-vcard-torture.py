"""
Torture tests for vcard manager, setting and getting of vcards.
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

@lazy
@match('stream-iq')
def expect_get_inital_vcard(event, data):
    iq = event.stanza

    if iq['type'] != 'get':
        return False

    if iq.uri != 'jabber:client':
        return False

    vcard = list(iq.elements())[0]

    if vcard.name != 'vCard':
        return False
    if vcard['xmlns'] != 'vcard-temp':
        return False

    time.sleep(40)
    return handle_get_vcard(event, data)

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    return True


@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

