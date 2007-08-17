
"""
Test alias setting support.
"""

import dbus
from servicetest import tp_name_prefix, match
from gabbletest import go

from twisted.words.protocols.jabber.client import IQ

def aliasing_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Aliasing')

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    aliasing_iface(data['conn_iface']).SetAliases({1: 'lala'})
    return True

@match('stream-iq', to=None, iq_type='get')
def expect_vcard_get_iq(event, data):
    vcard = event.stanza.firstChildElement()

    if vcard.name != 'vCard':
        return False

    iq = IQ(data['stream'], 'result')
    iq['id'] = event.stanza['id']
    data['stream'].send(iq)
    return True

@match('stream-iq', to=None, iq_type='set')
def expect_vcard_set_iq(event, data):
    vcard = event.stanza.firstChildElement()

    if vcard.name != 'vCard':
        return False

    iq = IQ(data['stream'], 'result')
    iq['id'] = event.stanza['id']
    data['stream'].send(iq)
    return True

@match('dbus-signal', signal='AliasesChanged')
def expect_AliasesChanged(event, data):
    assert event.args == [[(1, u'lala')]]
    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

