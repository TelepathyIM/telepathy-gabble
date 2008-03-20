
"""
Test capabilities.
"""

import dbus

from twisted.words.xish import domish

from servicetest import match
from gabbletest import go, make_result_iq

basic_caps = [
  (2, u'org.freedesktop.Telepathy.Channel.Type.Text', 3, 0),
  ]

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
    data['stream'].send(presence)
    return True

@match('dbus-signal', signal='PresenceUpdate',
    args=[{2L: (0L, {u'available': {'message': 'hello'}})}])
def expect_presence_update(event, data):
    # no special capabilities
    assert caps_iface(data['conn_iface']).GetCapabilities([2]) == basic_caps

    # send updated presence with Jingle caps info
    presence = make_presence('bob@foo.com/Foo', None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = 'http://telepathy.freedesktop.org/fake-client'
    c['ver'] = '0.1'
    data['stream'].send(presence)
    return True

@match('stream-iq', query_ns='http://jabber.org/protocol/disco#info',
    to='bob@foo.com/Foo')
def expect_disco_iq(event, data):
    # Gabble looks up our capabilities
    result = make_result_iq(data['stream'], event.stanza)
    query = result.firstChildElement()
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle'
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle/description/audio'
    data['stream'].send(result)
    return True

@match('dbus-signal', signal='CapabilitiesChanged',
    args=[[(2, u'org.freedesktop.Telepathy.Channel.Type.StreamedMedia', 0,
        3, 0, 1)]])
def expect_CapabilitiesChanged(event, data):
    # we can now do audio calls
    # go offline
    presence = make_presence('bob@foo.com/Foo', 'unavailable', None)
    data['stream'].send(presence)
    return True

@match('dbus-signal', signal='CapabilitiesChanged',
    args=[[(2, u'org.freedesktop.Telepathy.Channel.Type.StreamedMedia', 3,
        0, 1, 0)]])
def expect_CapabilitiesChanged2(event, data):
    # can't do calls any more
    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

