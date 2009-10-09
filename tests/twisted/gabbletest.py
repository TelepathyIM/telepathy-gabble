
"""
Infrastructure code for testing Gabble by pretending to be a Jabber server.
"""

import base64
import os
import hashlib
import sys
import random
import traceback

import ns
import constants as cs
import servicetest
from servicetest import (
    assertEquals, assertLength, assertContains, wrap_channel, EventPattern,
    )
import twisted
from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ
from twisted.words.protocols.jabber import xmlstream
from twisted.internet import reactor

import dbus

NS_XMPP_SASL = 'urn:ietf:params:xml:ns:xmpp-sasl'
NS_XMPP_BIND = 'urn:ietf:params:xml:ns:xmpp-bind'

def make_result_iq(stream, iq):
    result = IQ(stream, "result")
    result["id"] = iq["id"]
    to = iq.getAttribute('to')
    if to is not None:
        result["from"] = to
    query = iq.firstChildElement()

    if query:
        result.addElement((query.uri, query.name))

    return result

def acknowledge_iq(stream, iq):
    stream.send(make_result_iq(stream, iq))

def send_error_reply(stream, iq, error_stanza=None):
    result = IQ(stream, "error")
    result["id"] = iq["id"]
    query = iq.firstChildElement()
    to = iq.getAttribute('to')
    if to is not None:
        result["from"] = to

    if query:
        result.addElement((query.uri, query.name))

    if error_stanza:
        result.addChild(error_stanza)

    stream.send(result)

def request_muc_handle(q, conn, stream, muc_jid):
    servicetest.call_async(q, conn, 'RequestHandles', 2, [muc_jid])
    host = muc_jid.split('@')[1]
    event = q.expect('stream-iq', to=host, query_ns=ns.DISCO_INFO)
    result = make_result_iq(stream, event.stanza)
    feature = result.firstChildElement().addElement('feature')
    feature['var'] = ns.MUC
    stream.send(result)
    event = q.expect('dbus-return', method='RequestHandles')
    return event.value[0][0]

def make_muc_presence(affiliation, role, muc_jid, alias, jid=None):
    presence = domish.Element((None, 'presence'))
    presence['from'] = '%s/%s' % (muc_jid, alias)
    x = presence.addElement((ns.MUC_USER, 'x'))
    item = x.addElement('item')
    item['affiliation'] = affiliation
    item['role'] = role
    if jid is not None:
        item['jid'] = jid
    return presence

def sync_stream(q, stream):
    """Used to ensure that Gabble has processed all stanzas sent to it."""

    iq = IQ(stream, "get")
    id = iq['id']
    iq.addElement(('http://jabber.org/protocol/disco#info', 'query'))
    stream.send(iq)
    q.expect('stream-iq', query_ns='http://jabber.org/protocol/disco#info',
        predicate=(lambda event:
            event.stanza['id'] == id and event.iq_type == 'result'))

class JabberAuthenticator(xmlstream.Authenticator):
    "Trivial XML stream authenticator that accepts one username/digest pair."

    def __init__(self, username, password, resource=None):
        self.username = username
        self.password = password
        self.resource = resource
        xmlstream.Authenticator.__init__(self)

    # Patch in fix from http://twistedmatrix.com/trac/changeset/23418.
    # This monkeypatch taken from Gadget source code
    from twisted.words.xish.utility import EventDispatcher

    def _addObserver(self, onetime, event, observerfn, priority, *args,
            **kwargs):
        if self._dispatchDepth > 0:
            self._updateQueue.append(lambda: self._addObserver(onetime, event,
                observerfn, priority, *args, **kwargs))

        return self._oldAddObserver(onetime, event, observerfn, priority,
            *args, **kwargs)

    EventDispatcher._oldAddObserver = EventDispatcher._addObserver
    EventDispatcher._addObserver = _addObserver

    def streamStarted(self, root=None):
        if root:
            self.xmlstream.sid = '%x' % random.randint(1, sys.maxint)

        self.xmlstream.sendHeader()
        self.xmlstream.addOnetimeObserver(
            "/iq/query[@xmlns='jabber:iq:auth']", self.initialIq)

    def initialIq(self, iq):
        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        query = result.addElement('query')
        query["xmlns"] = "jabber:iq:auth"
        query.addElement('username', content='test')
        query.addElement('password')
        query.addElement('digest')
        query.addElement('resource')
        self.xmlstream.addOnetimeObserver('/iq/query/username', self.secondIq)
        self.xmlstream.send(result)

    def secondIq(self, iq):
        username = xpath.queryForNodes('/iq/query/username', iq)
        assert map(str, username) == [self.username]

        digest = xpath.queryForNodes('/iq/query/digest', iq)
        expect = hashlib.sha1(self.xmlstream.sid + self.password).hexdigest()
        assert map(str, digest) == [expect]

        resource = xpath.queryForNodes('/iq/query/resource', iq)
        assertLength(1, resource)
        if self.resource is not None:
            assertEquals(self.resource, str(resource[0]))

        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        self.xmlstream.send(result)
        self.xmlstream.dispatch(self.xmlstream, xmlstream.STREAM_AUTHD_EVENT)


class XmppAuthenticator(xmlstream.Authenticator):
    def __init__(self, username, password, resource=None):
        xmlstream.Authenticator.__init__(self)
        self.username = username
        self.password = password
        self.resource = resource
        self.authenticated = False

    def streamStarted(self, root=None):
        if root:
            self.xmlstream.sid = root.getAttribute('id')

        self.xmlstream.sendHeader()

        if self.authenticated:
            # Initiator authenticated itself, and has started a new stream.

            features = domish.Element((xmlstream.NS_STREAMS, 'features'))
            bind = features.addElement((NS_XMPP_BIND, 'bind'))
            self.xmlstream.send(features)

            self.xmlstream.addOnetimeObserver(
                "/iq/bind[@xmlns='%s']" % NS_XMPP_BIND, self.bindIq)
        else:
            features = domish.Element((xmlstream.NS_STREAMS, 'features'))
            mechanisms = features.addElement((NS_XMPP_SASL, 'mechanisms'))
            mechanism = mechanisms.addElement('mechanism', content='PLAIN')
            self.xmlstream.send(features)

            self.xmlstream.addOnetimeObserver("/auth", self.auth)

    def auth(self, auth):
        assert (base64.b64decode(str(auth)) ==
            '\x00%s\x00%s' % (self.username, self.password))

        success = domish.Element((NS_XMPP_SASL, 'success'))
        self.xmlstream.send(success)
        self.xmlstream.reset()
        self.authenticated = True

    def bindIq(self, iq):
        resource = xpath.queryForString('/iq/bind/resource', iq)
        if self.resource is not None:
            assertEquals(self.resource, resource)
        else:
            assert resource is not None

        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        bind = result.addElement((NS_XMPP_BIND, 'bind'))
        jid = bind.addElement('jid', content=('test@localhost/%s' % resource))
        self.xmlstream.send(result)

        self.xmlstream.dispatch(self.xmlstream, xmlstream.STREAM_AUTHD_EVENT)

def make_stream_event(type, stanza):
    event = servicetest.Event(type, stanza=stanza)
    event.to = stanza.getAttribute("to")
    return event

def make_iq_event(iq):
    event = make_stream_event('stream-iq', iq)
    event.iq_type = iq.getAttribute("type")
    event.iq_id = iq.getAttribute("id")
    query = iq.firstChildElement()

    if query:
        event.query = query
        event.query_ns = query.uri
        event.query_name = query.name

        if query.getAttribute("node"):
            event.query_node = query.getAttribute("node")
    else:
        event.query = None

    return event

def make_presence_event(stanza):
    event = make_stream_event('stream-presence', stanza)
    event.presence_type = stanza.getAttribute('type')
    return event

def make_message_event(stanza):
    event = make_stream_event('stream-message', stanza)
    event.message_type = stanza.getAttribute('type')
    return event

class BaseXmlStream(xmlstream.XmlStream):
    initiating = False
    namespace = 'jabber:client'

    def __init__(self, event_func, authenticator):
        xmlstream.XmlStream.__init__(self, authenticator)
        self.event_func = event_func
        self.addObserver('//iq', lambda x: event_func(
            make_iq_event(x)))
        self.addObserver('//message', lambda x: event_func(
            make_message_event(x)))
        self.addObserver('//presence', lambda x: event_func(
            make_presence_event(x)))
        self.addObserver('//event/stream/authd', self._cb_authd)

    def _cb_authd(self, _):
        # called when stream is authenticated
        self.addObserver(
            "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
            self._cb_disco_iq)
        self.event_func(servicetest.Event('stream-authenticated'))

    def _cb_disco_iq(self, iq):
        if iq.getAttribute('to') == 'localhost':
            # add PEP support
            nodes = xpath.queryForNodes(
                "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
                iq)
            query = nodes[0]
            identity = query.addElement('identity')
            identity['category'] = 'pubsub'
            identity['type'] = 'pep'

            iq['type'] = 'result'
            iq['from'] = iq['to']
            self.send(iq)

class JabberXmlStream(BaseXmlStream):
    version = (0, 9)

class XmppXmlStream(BaseXmlStream):
    version = (1, 0)

class GoogleXmlStream(BaseXmlStream):
    version = (1, 0)

    def _cb_disco_iq(self, iq):
        if iq.getAttribute('to') == 'localhost':
            nodes = xpath.queryForNodes(
                "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
                iq)
            query = nodes[0]
            feature = query.addElement('feature')
            feature['var'] = ns.GOOGLE_ROSTER
            feature = query.addElement('feature')
            feature['var'] = ns.GOOGLE_JINGLE_INFO

            iq['type'] = 'result'
            iq['from'] = 'localhost'
            self.send(iq)

def make_connection(bus, event_func, params=None):
    default_params = {
        'account': 'test@localhost/Resource',
        'password': 'pass',
        'resource': 'Resource',
        'server': 'localhost',
        'port': dbus.UInt32(4242),
        'fallback-socks5-proxies': dbus.Array([], signature='s'),
        }

    if params:
        default_params.update(params)

    return servicetest.make_connection(bus, event_func, 'gabble', 'jabber',
        default_params)

def make_stream(event_func, authenticator=None, protocol=None, port=4242, resource=None):
    # set up Jabber server

    if authenticator is None:
        authenticator = XmppAuthenticator('test', 'pass', resource=resource)

    if protocol is None:
        protocol = XmppXmlStream

    stream = protocol(event_func, authenticator)
    factory = twisted.internet.protocol.Factory()
    factory.protocol = lambda *args: stream
    port = reactor.listenTCP(port, factory)
    return (stream, port)

def disconnect_conn(q, conn, stream, expected=[]):
    conn.Disconnect()

    tmp = expected + [EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED])]

    events = q.expect_many(*tmp)
    return events[:-1]

def exec_test_deferred(fun, params, protocol=None, timeout=None,
                        authenticator=None):
    # hack to ease debugging
    domish.Element.__repr__ = domish.Element.toXml
    colourer = None

    if sys.stdout.isatty() or 'CHECK_FORCE_COLOR' in os.environ:
        colourer = servicetest.install_colourer()

    queue = servicetest.IteratingEventQueue(timeout)
    queue.verbose = (
        os.environ.get('CHECK_TWISTED_VERBOSE', '') != ''
        or '-v' in sys.argv)

    bus = dbus.SessionBus()
    conn = make_connection(bus, queue.append, params)
    resource = params.get('resource') if params is not None else None
    (stream, port) = make_stream(queue.append, protocol=protocol,
        authenticator=authenticator, resource=resource)

    error = None

    try:
        fun(queue, bus, conn, stream)
    except Exception, e:
        traceback.print_exc()
        error = e

    if colourer:
        sys.stdout = colourer.fh

    d = port.stopListening()

    if error is None:
        d.addBoth((lambda *args: reactor.crash()))
    else:
        # please ignore the POSIX behind the curtain
        d.addBoth((lambda *args: os._exit(1)))

    try:
        disconnect_conn(queue, conn, stream)
    except dbus.DBusException, e:
        # Connection has already been disconnected
        pass

def exec_test(fun, params=None, protocol=None, timeout=None,
              authenticator=None):
    reactor.callWhenRunning(
        exec_test_deferred, fun, params, protocol, timeout, authenticator)
    reactor.run()

# Useful routines for server-side vCard handling
current_vcard = domish.Element(('vcard-temp', 'vCard'))

def expect_and_handle_get_vcard(q, stream):
    get_vcard_event = q.expect('stream-iq', query_ns=ns.VCARD_TEMP,
        query_name='vCard', iq_type='get')

    iq = get_vcard_event.stanza
    vcard = iq.firstChildElement()
    assert vcard.name == 'vCard', vcard.toXml()

    # Send back current vCard
    result = make_result_iq(stream, iq)
    result.addChild(current_vcard)
    stream.send(result)

def expect_and_handle_set_vcard(q, stream, check=None):
    set_vcard_event = q.expect('stream-iq', query_ns=ns.VCARD_TEMP,
        query_name='vCard', iq_type='set')
    iq = set_vcard_event.stanza
    vcard = iq.firstChildElement()
    assert vcard.name == 'vCard', vcard.toXml()

    if check is not None:
        check(vcard)

    # Update current vCard
    current_vcard = vcard

    stream.send(make_result_iq(stream, iq))

def _elem_add(elem, *children):
    for child in children:
        if isinstance(child, domish.Element):
            elem.addChild(child)
        elif isinstance(child, unicode):
            elem.addContent(child)
        else:
            raise ValueError(
                'invalid child object %r (must be element or unicode)', child)

def elem(a, b=None, **kw):
    r"""
    >>> elem('foo')().toXml()
    u'<foo/>'
    >>> elem('foo', x='1')().toXml()
    u"<foo x='1'/>"
    >>> elem('foo', x='1')(u'hello').toXml()
    u"<foo x='1'>hello</foo>"
    >>> elem('foo', x='1')(u'hello',
    ...         elem('http://foo.org', 'bar', y='2')(u'bye')).toXml()
    u"<foo x='1'>hello<bar xmlns='http://foo.org' y='2'>bye</bar></foo>"
    """

    class _elem(domish.Element):
        def __call__(self, *children):
            _elem_add(self, *children)
            return self

    if b is not None:
        elem = _elem((a, b))
    else:
        elem = _elem((None, a))

    for k, v in kw.iteritems():
        if k == 'from_':
            elem['from'] = v
        else:
            elem[k] = v

    return elem

def elem_iq(server, type, **kw):
    class _iq(IQ):
        def __call__(self, *children):
            _elem_add(self, *children)
            return self

    iq = _iq(server, type)

    for k, v in kw.iteritems():
        if k == 'from_':
            iq['from'] = v
        else:
            iq[k] = v

    return iq

def make_presence(_from, to='test@localhost', type=None, show=None,
        status=None, caps=None, photo=None):
    presence = domish.Element((None, 'presence'))
    presence['from'] = _from
    presence['to'] = to

    if type is not None:
        presence['type'] = type

    if show is not None:
        presence.addElement('show', content=show)

    if status is not None:
        presence.addElement('status', content=status)

    if caps is not None:
        cel = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
        for key,value in caps.items():
            cel[key] = value

    # <x xmlns="vcard-temp:x:update"><photo>4a1...</photo></x>
    if photo is not None:
        x = presence.addElement((ns.VCARD_TEMP_UPDATE, 'x'))
        x.addElement('photo').addContent(photo)

    return presence

def expect_list_channel(q, bus, conn, name, contacts, lp_contacts=[],
                        rp_contacts=[]):
    return expect_contact_list_channel(q, bus, conn, cs.HT_LIST, name,
        contacts, lp_contacts=lp_contacts, rp_contacts=rp_contacts)

def expect_group_channel(q, bus, conn, name, contacts, lp_contacts=[],
                         rp_contacts=[]):
    return expect_contact_list_channel(q, bus, conn, cs.HT_GROUP, name,
        contacts, lp_contacts=lp_contacts, rp_contacts=rp_contacts)

def expect_contact_list_channel(q, bus, conn, ht, name, contacts,
                                lp_contacts=[], rp_contacts=[]):
    """
    Expects NewChannel and NewChannels signals for the
    contact list with handle type 'ht' and ID 'name', and checks that its
    members, lp members and rp members are exactly 'contacts', 'lp_contacts'
    and 'rp_contacts'.
    Returns a proxy for the channel.
    """

    old_signal, new_signal = q.expect_many(
            EventPattern('dbus-signal', signal='NewChannel'),
            EventPattern('dbus-signal', signal='NewChannels'),
            )

    path, type, handle_type, handle, suppress_handler = old_signal.args

    assertEquals(cs.CHANNEL_TYPE_CONTACT_LIST, type)
    assertEquals(name, conn.InspectHandles(handle_type, [handle])[0])

    chan = wrap_channel(bus.get_object(conn.bus_name, path),
        cs.CHANNEL_TYPE_CONTACT_LIST)
    members = chan.Group.GetMembers()

    assertEquals(sorted(contacts),
        sorted(conn.InspectHandles(cs.HT_CONTACT, members)))

    lp_handles = conn.RequestHandles(cs.HT_CONTACT, lp_contacts)
    rp_handles = conn.RequestHandles(cs.HT_CONTACT, rp_contacts)

    # NB. comma: we're unpacking args. Thython!
    info, = new_signal.args
    assertLength(1, info) # one channel
    path_, emitted_props = info[0]

    assertEquals(path_, path)

    assertEquals(cs.CHANNEL_TYPE_CONTACT_LIST, emitted_props[cs.CHANNEL_TYPE])
    assertEquals(ht, emitted_props[cs.TARGET_HANDLE_TYPE])
    assertEquals(handle, emitted_props[cs.TARGET_HANDLE])

    channel_props = chan.Properties.GetAll(cs.CHANNEL)
    assertEquals(handle, channel_props.get('TargetHandle'))
    assertEquals(ht, channel_props.get('TargetHandleType'))
    assertEquals(cs.CHANNEL_TYPE_CONTACT_LIST, channel_props.get('ChannelType'))
    assertContains(cs.CHANNEL_IFACE_GROUP, channel_props.get('Interfaces'))
    assertEquals(name, channel_props['TargetID'])
    assertEquals(False, channel_props['Requested'])
    assertEquals('', channel_props['InitiatorID'])
    assertEquals(0, channel_props['InitiatorHandle'])

    group_props = chan.Properties.GetAll(cs.CHANNEL_IFACE_GROUP)
    assertContains('HandleOwners', group_props)
    assertContains('Members', group_props)
    assertEquals(members, group_props['Members'])
    assertContains('LocalPendingMembers', group_props)
    actual_lp_handles = [x[0] for x in group_props['LocalPendingMembers']]
    assertEquals(sorted(lp_handles), sorted(actual_lp_handles))
    assertContains('RemotePendingMembers', group_props)
    assertEquals(sorted(rp_handles), sorted(group_props['RemotePendingMembers']))
    assertContains('GroupFlags', group_props)

    return chan
