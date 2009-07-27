# vim: set fileencoding=utf-8 :
import hashlib
import base64
import dbus

from twisted.words.xish import domish, xpath
from gabbletest import make_result_iq
from servicetest import EventPattern, assertEquals

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

ft_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
    })
ft_allowed_properties = dbus.Array([
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType',
    cs.TARGET_HANDLE,
    cs.TARGET_ID,
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.ContentType',
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.Filename',
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.Size',
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash',
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.Description',
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.Date'])

fake_client_dataforms = {
    'urn:xmpp:dataforms:softwareinfo':
    {'software': ['A Fake Client with Twisted'],
        'software_version': ['5.11.2-svn-20080512'],
        'os': ['Debian GNU/Linux unstable (sid) unstable sid'],
        'os_version': ['2.6.24-1-amd64'],
    },
}

def compute_caps_hash(identities, features, dataforms):
    """
    Accepts a list of slash-separated identities, a list of feature namespaces,
    and a map from FORM_TYPE to (map from field name to values), returns the
    verification string as defined by
    <http://xmpp.org/extensions/xep-0115.html#ver>.
    """
    components = []

    for identity in sorted(identities):
        components.append(identity)

    for feature in sorted(features):
        components.append(feature)

    for form_type in sorted(dataforms.keys()):
        components.append(form_type)

        for var in sorted(dataforms[form_type].keys()):
            components.append(var)

            for value in sorted(dataforms[form_type][var]):
                components.append(value)

    components.append('')

    m = hashlib.sha1()
    S = u'<'.join(components)
    m.update(S.encode('utf-8'))
    return base64.b64encode(m.digest())

def make_caps_disco_reply(stream, req, features, dataforms={}):
    iq = make_result_iq(stream, req)
    query = iq.firstChildElement()

    for f in features:
        el = domish.Element((None, 'feature'))
        el['var'] = f
        query.addChild(el)

    for type, fields in dataforms.iteritems():
        x = query.addElement((ns.X_DATA, 'x'))
        x['type'] = 'result'

        field = x.addElement('field')
        field['var'] = 'FORM_TYPE'
        field['type'] = 'hidden'
        field.addElement('value', content=type)

        for var, values in fields.iteritems():
            field = x.addElement('field')
            field['var'] = var

            for value in values:
                field.addElement('value', content=value)

    return iq

def receive_presence_and_ask_caps(q, stream, expect_dbus=True):
    # receive presence stanza
    if expect_dbus:
        event_stream, event_dbus = q.expect_many(
                EventPattern('stream-presence'),
                EventPattern('dbus-signal', signal='ContactCapabilitiesChanged')
            )
        assert len(event_dbus.args) == 1
        signaled_caps = event_dbus.args[0]
    else:
        event_stream = q.expect('stream-presence')
        signaled_caps = None

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
    assert ver == compute_caps_hash(['client/pc//%s' % PACKAGE_STRING], features, {})

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
        {}) == 'QgayPKawpkPSDYmwT/WM94uAlu0='

    # another example from XEP-0115
    identities = [u'client/pc/en/Psi 0.11', u'client/pc/el/Ψ 0.11']
    features = [
        u'http://jabber.org/protocol/caps',
        u'http://jabber.org/protocol/disco#info',
        u'http://jabber.org/protocol/disco#items',
        u'http://jabber.org/protocol/muc',
        ]
    dataforms = {
        u'urn:xmpp:dataforms:softwareinfo':
            { u'ip_version': [u'ipv4', u'ipv6'],
              u'os': [u'Mac'],
              u'os_version': [u'10.5.1'],
              u'software': [u'Psi'],
              u'software_version': [u'0.11'],
            },
        }
    assertEquals('q07IKJEyjvHSyhy//CH0CxmKi8w=',
        compute_caps_hash(identities, features, dataforms))
