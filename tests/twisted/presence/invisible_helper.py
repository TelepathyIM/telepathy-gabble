from gabbletest import XmppXmlStream, IQ, elem, elem_iq
import ns
from twisted.words.xish import xpath

class Xep0186XmlStream(XmppXmlStream):
    disco_features = [ns.INVISIBLE]

class Xep0126XmlStream(XmppXmlStream):
    handle_privacy_lists = False

    def handle_get_all_privacy_lists(self, q, bus, conn, lists=[]):
        list_elements = [elem('list', name=l) for l in lists]

        e = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='get')
        iq = elem_iq(self, "result", id=e.stanza["id"])(
            elem(ns.PRIVACY, 'query')(*list_elements))
        self.send(iq)

    def send_privacy_list_push_iq(self, list_name):
        iq = elem_iq(self, 'set')(
            elem(ns.PRIVACY, 'query')(
                elem('list', name=list_name)
                )
            )
        self.send(iq)
        return iq["id"]

    def send_privacy_list(self, req_iq, list_items):
        req_list = xpath.queryForNodes('//list', req_iq)[0]
        iq = elem_iq(self, "result", id=req_iq["id"])(
            elem(ns.PRIVACY, 'query')(
                elem('list', name=req_list["name"])(*list_items)
                )
            )
        self.send(iq)
