import hashlib
import base64
import dbus

from twisted.words.xish import domish, xpath
from gabbletest import make_result_iq
from servicetest import EventPattern

from config import PACKAGE_STRING
import ns
import constants as cs

text_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT
    })
text_allowed_properties = dbus.Array([cs.TARGET_HANDLE])

stream_tube_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE
    })
stream_tube_allowed_properties = dbus.Array([cs.TARGET_HANDLE,
    cs.TARGET_ID, cs.STREAM_TUBE_SERVICE])

dbus_tube_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE
    })
dbus_tube_allowed_properties = dbus.Array([cs.TARGET_HANDLE,
    cs.TARGET_ID, cs.DBUS_TUBE_SERVICE_NAME])

def compute_caps_hash(identities, features, dataforms):
    S = ''

    for identity in sorted(identities):
        S += '%s<' % identity

    for feature in sorted(features):
        S += '%s<' % feature

    # FIXME: support dataforms

    m = hashlib.sha1()
    m.update(S)
    return base64.b64encode(m.digest())

def make_caps_disco_reply(stream, req, features):
    iq = make_result_iq(stream, req)
    query = iq.firstChildElement()

    for f in features:
        el = domish.Element((None, 'feature'))
        el['var'] = f
        query.addChild(el)

    return iq

def receive_presence_and_ask_caps(q, stream):
    # receive presence stanza
    event_stream, event_dbus = q.expect_many(
            EventPattern('stream-presence'),
            EventPattern('dbus-signal', signal='ContactCapabilitiesChanged')
        )
    assert len(event_dbus.args) == 1
    signaled_caps = event_dbus.args[0]

    c_nodes = xpath.queryForNodes('/presence/c', event_stream.stanza)
    assert c_nodes is not None
    assert len(c_nodes) == 1
    hash = c_nodes[0].attributes['hash']
    ver = c_nodes[0].attributes['ver']
    node = c_nodes[0].attributes['node']
    assert hash == 'sha-1'

    # ask caps
    request = """
<iq from='fake_contact@jabber.org/resource' 
    id='disco1'
    to='gabble@jabber.org/resource' 
    type='get'>
  <query xmlns='""" + ns.DISCO_INFO + """'
         node='""" + node + '#' + ver + """'/>
</iq>
"""
    stream.send(request)

    # receive caps
    event = q.expect('stream-iq', query_ns=ns.DISCO_INFO)
    caps_str = str(xpath.queryForNodes('/iq/query/feature', event.stanza))

    features = []
    for feature in xpath.queryForNodes('/iq/query/feature', event.stanza):
        features.append(feature['var'])

    # Check if the hash matches the announced capabilities
    assert ver == compute_caps_hash(['client/pc//%s' % PACKAGE_STRING], features, [])

    return (event, caps_str, signaled_caps)

def caps_contain(event, cap):
    node = xpath.queryForNodes('/iq/query/feature[@var="%s"]'
            % cap,
            event.stanza)
    if node is None:
        return False
    if len(node) != 1:
        return False
    var = node[0].attributes['var']
    if var is None:
        return False
    return var == cap

if __name__ == '__main__':
    # example from XEP-0115
    assert compute_caps_hash(['client/pc//Exodus 0.9.1'],
        ["http://jabber.org/protocol/disco#info",
        "http://jabber.org/protocol/disco#items",
        "http://jabber.org/protocol/muc", "http://jabber.org/protocol/caps"],
        []) == 'QgayPKawpkPSDYmwT/WM94uAlu0='
