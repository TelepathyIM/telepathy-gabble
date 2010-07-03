from gabbletest import XmppXmlStream, IQ
import ns
from twisted.words.xish import xpath

class Xep0186XmlStream(XmppXmlStream):
    disco_features = [ns.INVISIBLE]

def send_privacy_list_push_iq(stream, list_name):
    iq = IQ(stream, "set")
    iq.addUniqueId()
    iq.addRawXml(
        "<query xmlns='jabber:iq:privacy'><list name='%s' /></query>" % \
            list_name)
    stream.send(iq)
    return iq["id"]

def send_privacy_list(stream, req_iq, list_items):
    iq = IQ(stream, "result")
    iq["id"] = req_iq["id"]
    query = iq.addElement((ns.PRIVACY, "query"))
    req_list = xpath.queryForNodes('//list', req_iq)[0]
    l = query.addElement('list')
    l["name"] = req_list["name"]
    l.addRawXml(list_items)
    stream.send(iq)
