from gabbletest import XmppXmlStream, acknowledge_iq, send_error_reply
import ns
from twisted.words.xish import domish, xpath
from functools import partial

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
                    self.addObserver(pattern, partial(acknowledge_iq, self))
                else:
                    self.addObserver(pattern, partial(send_error_reply, self))

            self.send(iq)
