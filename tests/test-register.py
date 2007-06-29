
"""
Test registration.
"""

from gabbletest import go
from servicetest import match

from twisted.words.xish import xpath
from twisted.words.protocols.jabber.client import IQ

@match('dbus-signal', signal='StatusChanged', args=[1, 1])
def expect_connecting(event, data):
    return True

@match('stream-iq', query_ns='jabber:iq:register')
def expect_register_iq1(event, data):
    iq = event.stanza
    result = IQ(data['stream'], "result")
    result["id"] = iq["id"]
    query = result.addElement('query')
    query["xmlns"] = "jabber:iq:register"
    query.addElement('username')
    query.addElement('password')
    data['stream'].send(result)
    return True

@match('stream-iq')
def expect_register_iq2(event, data):
    iq = event.stanza
    assert xpath.queryForString('/iq/query/username', iq) == 'test'
    assert xpath.queryForString('/iq/query/password', iq) == 'pass'

    iq["type"] = "result"
    data['stream'].send(iq)
    return True

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go({'register': True})

