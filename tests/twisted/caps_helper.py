import hashlib
import base64

from twisted.words.xish import domish
from gabbletest import make_result_iq

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

if __name__ == '__main__':
    # example from XEP-0115
    assert compute_caps_hash(['client/pc//Exodus 0.9.1'],
        ["http://jabber.org/protocol/disco#info",
        "http://jabber.org/protocol/disco#items",
        "http://jabber.org/protocol/muc", "http://jabber.org/protocol/caps"],
        []) == 'QgayPKawpkPSDYmwT/WM94uAlu0='
