
"""
Test alias support.
"""

import base64

import dbus
from servicetest import tp_name_prefix, call_async, match
from gabbletest import go, make_result_iq

def aliasing_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Aliasing')

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    handle = data['conn_iface'].RequestHandles(1, ['bob@foo.com'])[0]
    call_async(data['test'], aliasing_iface(data['conn']), 'RequestAliases',
        [handle])
    data['handle'] = handle
    return True

@match('stream-iq', to=None)
def expect_my_vcard_iq(event, data):
    vcard = event.stanza.firstChildElement()

    if vcard.name != 'vCard':
        return False

    data['stream'].send(make_result_iq(data['stream'], event.stanza))
    return True

@match('stream-iq', to='bob@foo.com')
def expect_vcard_iq(event, data):
    iq = event.stanza
    vcard = iq.firstChildElement()
    assert vcard.name == 'vCard'

    result = make_result_iq(data['stream'], iq)
    result.addChild(vcard)

    vcard.addElement('NICKNAME', content='Bobby')

    data['stream'].send(result)
    return True

@match('dbus-signal', signal='AliasesChanged')
def expect_AliasesChanged(event, data):
    return event.args == [[(data['handle'], u'Bobby')]]

@match('dbus-return', method='RequestAliases')
def expect_RequestAliases_return(event, data):
    assert event.value[0] == ['Bobby']
    # A second request should be satisfied from the cache.
    assert aliasing_iface(data['conn']).RequestAliases(
        [data['handle']]) == ['Bobby']
    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

