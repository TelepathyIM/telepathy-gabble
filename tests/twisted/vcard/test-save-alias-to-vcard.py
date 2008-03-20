
"""
Regression test.

 - the 'alias' connection parameter is set
 - our vCard doesn't have a NICKNAME field
 - we crash when trying to save a vcard with NICKNAME set to the alias
   parameter
"""

from servicetest import lazy
from gabbletest import go

@lazy
def expect_connected(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'StatusChanged':
        return False

    if event.args != [0, 1]:
        return False

    return True

def expect_get_vcard(event, data):
    if event.type != 'stream-iq':
        return False

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
    return True

def expect_set_vcard(event, data):
    if event.type != 'stream-iq':
        return False

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
    go({'alias': 'Some Guy'})

