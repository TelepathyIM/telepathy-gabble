from gabbletest import XmppXmlStream, elem_iq
import ns
from twisted.words.xish import domish, xpath

class InvisibleXmlStream(XmppXmlStream):
    FEATURES = [ns.INVISIBLE, ns.PRIVACY]
    RESPONDERS = {"/iq/invisible[@xmlns='urn:xmpp:invisible:0']" : True,
                  "/iq/visible[@xmlns='urn:xmpp:invisible:0']" : True,
                  "/iq/query[@xmlns='jabber:iq:privacy']" : True}
    def _cb_disco_iq(self, iq):
        if iq.getAttribute('to') == 'localhost':
            nodes = xpath.queryForNodes(
                "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
                iq)
            query = nodes[0]

            for feature in self.FEATURES:
                f = query.addElement('feature')
                f['var'] = feature

            iq['type'] = 'result'
            iq['from'] = iq['to']
            del iq['to']

            for pattern, success in self.RESPONDERS.items():
                if success:
                    self.addObserver(
                        pattern, self._cb_invisible_success)
                else:
                    self.addObserver(
                        pattern, self._cb_invisible_fail)

            self.send(iq)

    def _cb_invisible_success(self, iq):
        reply = elem_iq(self, 'result', id=iq["id"])
        self.send(reply)

    def _cb_invisible_fail(self, iq):
        reply = elem_iq(self, 'error', id=iq["id"])
        self.send(reply)
