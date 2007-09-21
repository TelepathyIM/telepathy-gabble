
"""
Test that CapabilitiesChanged signal is emitted only once after
all the caps in the presence have been analyzed.
"""

import dbus

from twisted.words.xish import domish

from servicetest import match, unwrap, lazy
from gabbletest import go, make_result_iq

def make_presence(from_jid, type, status):
    presence = domish.Element((None, 'presence'))

    if from_jid is not None:
        presence['from'] = from_jid

    if type is not None:
        presence['type'] = type

    if status is not None:
        presence.addElement('status', content=status)

    return presence

def caps_iface(proxy):
    return dbus.Interface(proxy,
        'org.freedesktop.Telepathy.Connection.Interface.Capabilities')

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    presence = make_presence('bob@foo.com/Foo', None, 'hello')
    presence.addElement('priority', None, '0')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = 'http://telepathy.freedesktop.org/caps'
    c['ver'] = '0.5.14'
    c['ext'] = 'voice-v1 jingle-audio jingle-video'
    data['stream'].send(presence)
    return True

@lazy
@match('dbus-signal', signal='CapabilitiesChanged',
    args=[[(2, u'org.freedesktop.Telepathy.Channel.Type.StreamedMedia', 0,
        3, 0, 3)]])
def expect_CapabilitiesChanged(event, data):
    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal')
def expect_disconnected(event, data):
    assert event.signal != 'CapabilitiesChanged'
    if event.signal == 'StatusChanged' and event.args == [2, 1]:
        return True
    return False

if __name__ == '__main__':
    go()

