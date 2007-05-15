
from twisted.internet import glib2reactor
glib2reactor.install()

from twisted.words.xish import domish

from gabbletest import EventTest, run

def expect_connected(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    if event[3] != [0, 1]:
        return False

    m = domish.Element(('', 'iq'))
    m['from'] = 'foo@bar.com'
    m['id'] = '1'
    query = m.addElement('query')
    query['xmlns'] = 'http://jabber.org/protocol/disco#info'
    data['stream'].send(m)
    return True

def expect_disco_response(event, data):
    if event[0] != 'stream-iq':
        return False

    elem = event[1]

    if elem['id'] != '1':
        return False

    assert elem['type'] == 'result'
    assert elem['to'] == 'foo@bar.com'
    data['conn'].Disconnect()
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
    test = EventTest()
    map(test.expect, [
        expect_connected,
        expect_disco_response,
        expect_disconnected,
        ])
    run(test)

