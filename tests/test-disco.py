
"""
Test that Gabble responds to disco#info queries.
"""

from twisted.words.xish import domish

from gabbletest import go

def expect_connected(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'StatusChanged':
        return False

    if event.args != [0, 1]:
        return False

    m = domish.Element(('', 'iq'))
    m['from'] = 'foo@bar.com'
    m['id'] = '1'
    query = m.addElement('query')
    query['xmlns'] = 'http://jabber.org/protocol/disco#info'
    data['stream'].send(m)
    return True

def expect_disco_response(event, data):
    if event.type != 'stream-iq':
        return False

    elem = event.stanza

    if elem['id'] != '1':
        return False

    assert elem['type'] == 'result'
    assert elem['to'] == 'foo@bar.com'
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

