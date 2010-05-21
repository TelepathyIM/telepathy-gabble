from gabbletest import XmppXmlStream
import ns

class Xep0186XmlStream(XmppXmlStream):
    disco_features = [ns.INVISIBLE]

class PrivacyListXmlStream(XmppXmlStream):
    disco_features = [ns.PRIVACY]

class Xep0186PrivacyXmlStream(XmppXmlStream):
    # We advertise privacy as well to test that Gabble prefers XEP-0186.
    disco_features = [ns.INVISIBLE, ns.PRIVACY]
