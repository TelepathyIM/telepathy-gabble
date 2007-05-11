
import dbus

from twisted.internet import glib2reactor
glib2reactor.install()

from twisted.words.xish import domish
from twisted.internet import reactor

from gabbletest import EventTest, conn_iface, gabble_test_setup, run

def expect_connected(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    if event[3] != [0, 1]:
        return False

    # <message><body>hello</body</message>
    m = domish.Element(('', 'message'))
    m['from'] = 'foo@bar.com'
    m.addElement('body', content='hello')
    data['stream'].send(m)
    return True

def expect_new_channel(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'NewChannel':
        return False

    bus = data['conn']._bus
    data['text_chan'] = bus.get_object(
        data['conn']._named_service, event[3][0])

    if event[3][1] != u'org.freedesktop.Telepathy.Channel.Type.Text':
        return False

    # check that handle type == contact handle
    assert event[3][2] == 1

    jid = conn_iface(data['conn']).InspectHandles(1, [event[3][3]])[0]
    assert jid == 'foo@bar.com'
    return True

def expect_conn_received(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'Received':
        return False

    #print 'wrong message body'
    assert event[3][5] == 'hello'

    dbus.Interface(data['text_chan'],
        u'org.freedesktop.Telepathy.Channel.Type.Text').Send(0, 'goodbye')
    return True

def expect_srv_received(event, data):
    if event[0] != 'stream-message':
        return False

    elem = event[1]
    assert elem.name == 'message'
    body = list(event[1].elements())[0]
    assert body.name == 'body'
    assert body.children[0] == u'goodbye'

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
        expect_new_channel,
        expect_conn_received,
        expect_srv_received,
        expect_disconnected,
        ])
    run(test)

