from gabbletest import (
    XmppXmlStream, elem, elem_iq, send_error_reply, acknowledge_iq
)
import ns
from twisted.words.xish import xpath

class ManualPrivacyListStream(XmppXmlStream):
    """Unlike the base class, which automatically responds to privacy list
    requests in the negative, this stream class does not automatically respond.
    Instead it provides helper methods to let your test expect requests and
    respond to them manually."""

    handle_privacy_lists = False

    def send_privacy_list_list(self, iq_id, lists=[]):
        list_elements = [elem('list', name=l) for l in lists]

        iq = elem_iq(self, "result", id=iq_id)(
            elem(ns.PRIVACY, 'query')(*list_elements))
        self.send(iq)

    def handle_get_all_privacy_lists(self, q, bus, conn, lists=[]):
        e = q.expect('stream-iq', query_ns=ns.PRIVACY, iq_type='get')

        self.send_privacy_list_list(e.iq_id, lists)

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

class ValidInvisibleListStream(ManualPrivacyListStream):
    """This stream class pretends to be a server which supports privacy lists.
    It has exactly one stored list, named 'invisible', which satisfies Gabble's
    idea of what an invisible list should look like. Activating that list, or the  Any attempts to modify the
    stored lists, or activate one, will fail.

    The intention is that this class could be used to run presence tests
    unrelated to invisibility against a server which supports invisibility."""

    def __init__(self, event_func, authenticator):
        ManualPrivacyListStream.__init__(self, event_func, authenticator)

        self.addObserver("/iq/query[@xmlns='%s']" % ns.PRIVACY,
            self.privacy_list_iq_cb)

    def privacy_list_iq_cb(self, iq):
        if iq.getAttribute("type") == 'set':
            active = xpath.queryForNodes("//active", iq)

            if active and ('name' not in active[0].attributes or
                           active[0]['name'] == 'invisible'):
                acknowledge_iq(self, iq)
            else:
                # Don't allow other lists to be activated; and don't allow
                # modifications.
                send_error_reply(self, iq)
        else:
            requested_lists = xpath.queryForNodes('//list', iq)

            if not requested_lists:
                self.send_privacy_list_list(iq['id'], ['invisible'])
            elif requested_lists[0]['name'] == 'invisible':
                self.send_privacy_list(iq,
                    [elem('item', action='deny', order='1')(
                        elem('presence-out')
                      )
                    ])
            else:
                send_error_reply(self, iq, elem(ns.STANZA, 'item-not-found'))

class Xep0186Stream(XmppXmlStream):
    disco_features = [ns.INVISIBLE]

class Xep0186AndValidInvisibleListStream(ValidInvisibleListStream):
    disco_features = [ns.INVISIBLE]

class Xep0186AndManualPrivacyListStream(ManualPrivacyListStream):
    disco_features = [ns.INVISIBLE]
