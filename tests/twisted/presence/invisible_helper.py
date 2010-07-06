from gabbletest import XmppXmlStream, IQ, elem, elem_iq
import ns
from twisted.words.xish import xpath

class Xep0186XmlStream(XmppXmlStream):
    disco_features = [ns.INVISIBLE]

def send_privacy_list_push_iq(stream, list_name):
    iq = elem_iq(stream, 'set')(
        elem(ns.PRIVACY, 'query')(
            elem('list', name=list_name)
            )
        )
    stream.send(iq)
    return iq["id"]

def send_privacy_list(stream, req_iq, list_items):
    req_list = xpath.queryForNodes('//list', req_iq)[0]
    iq = elem_iq(stream, "result", id=req_iq["id"])(
        elem(ns.PRIVACY, 'query')(
            elem('list', name=req_list["name"])(*list_items)
            )
        )
    stream.send(iq)
