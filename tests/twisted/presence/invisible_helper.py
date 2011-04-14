from gabbletest import (
    XmppXmlStream, elem, elem_iq, send_error_reply, acknowledge_iq
)
import ns
from twisted.words.xish import xpath
from time import time

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

class SharedStatusStream(XmppXmlStream):
    disco_features = [ns.GOOGLE_SHARED_STATUS]
    def __init__(self, event_func, authenticator):
        XmppXmlStream.__init__(self, event_func, authenticator)
        self.addObserver("/iq/query[@xmlns='%s']" % ns.GOOGLE_SHARED_STATUS,
           self.shared_status_iq_cb)

        self.shared_status_lists = {u'default' : [u'Pining away',
                                                  u'Wherefore indeed',
                                                  u'Thinking about the sun'],
                                     u'dnd' : [u'Chilling with Mercutio',
                                              u'Visiting the monk']}

        self.shared_status = (u'Pining away', u'default', u'false')

        self.min_version = '2'

        self.max_status_message_length = '512'
        self.max_statuses = '5'

    def set_shared_status_lists(self, shared_status_lists=None, status=None,
                                show=None, invisible=None, min_version=None):
        self.shared_status_lists = shared_status_lists or \
            self.shared_status_lists
        _status, _show, _invisible = self.shared_status
        self.shared_status = (status or _status,
                              show or _show,
                              invisible or _invisible)

        self.min_version = min_version or self.min_version

        self._send_status_list()

    def _send_status_list(self, iq_id=None):
        if iq_id is None:
            iq_id = str(int(time()))
            iq_type = "set"
        else:
            iq_type = "result"
        status, show, invisible = self.shared_status
        elems = []
        elems.append(elem('status')(unicode(status)))
        elems.append(elem('show')(unicode(show)))
        for show, statuses in self.shared_status_lists.items():
            lst = []
            for _status in statuses:
                lst.append(elem('status')(unicode(_status)))
            elems.append(elem('status-list', show=show)(*lst))
        elems.append(elem('invisible', value=invisible)())

        attribs = {'status-max' : self.max_status_message_length,
                   'status-list-max' : '3',
                   'status-list-contents-max' : self.max_statuses,
                   'status-min-ver' : self.min_version}

        iq = elem_iq(self, iq_type, id=iq_id)(
            elem(ns.GOOGLE_SHARED_STATUS, 'query', **attribs)(*elems))

        self.send(iq)

    def _store_shared_statuses(self, iq):
        _status = xpath.queryForNodes('//status', iq)[0]
        _show = xpath.queryForNodes('//show', iq)[0]
        _invisible = xpath.queryForNodes('//invisible', iq)[0]
        self.shared_status = (str(_status),
                              str(_show),
                              _invisible.getAttribute('value'))

        _status_lists = xpath.queryForNodes('//status-list', iq)
        self.shared_status_lists = {}
        for s in _status_lists:
            self.shared_status_lists[s.getAttribute('show')] = \
                [str(e) for e in xpath.queryForNodes('//status', s)]

    def shared_status_iq_cb(self, req_iq):
        if req_iq.getAttribute("type") == 'get':
            self._send_status_list(req_iq['id'])
        if req_iq.getAttribute("type") == 'set':
            self._store_shared_statuses(req_iq)
            self.send(elem_iq(self, "result", id=req_iq['id'])())
