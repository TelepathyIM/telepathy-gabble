
"""
Infrastructure code for testing Gabble by pretending to be a Jabber server.
"""

import base64
import os
import hashlib
import sys
import random
import re
import traceback

import ns
import constants as cs
import servicetest
from servicetest import (
    assertEquals, assertLength, assertContains, wrap_channel,
    EventPattern, call_async, unwrap, Event)
import twisted
from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ
from twisted.words.protocols.jabber import xmlstream
from twisted.internet import reactor, ssl

import dbus

def make_result_iq(stream, iq, add_query_node=True):
    result = IQ(stream, "result")
    result["id"] = iq["id"]
    to = iq.getAttribute('to')
    if to is not None:
        result["from"] = to
    query = iq.firstChildElement()

    if query and add_query_node:
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
    event = q.expect('dbus-return', method='RequestHandles')
    return event.value[0][0]

def make_muc_presence(affiliation, role, muc_jid, alias, jid=None, photo=None):
    presence = domish.Element((None, 'presence'))
    presence['from'] = '%s/%s' % (muc_jid, alias)
    x = presence.addElement((ns.MUC_USER, 'x'))
    item = x.addElement('item')
    item['affiliation'] = affiliation
    item['role'] = role
    if jid is not None:
        item['jid'] = jid

    if photo is not None:
        presence.addChild(
            elem(ns.VCARD_TEMP_UPDATE, 'x')(
              elem('photo')(unicode(photo))
            ))

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

class GabbleAuthenticator(xmlstream.Authenticator):
    def __init__(self, username, password, resource=None):
        self.username = username
        self.password = password
        self.resource = resource
        self.bare_jid = None
        self.full_jid = None
        self._event_func = lambda e: None
        xmlstream.Authenticator.__init__(self)

    def set_event_func(self, event_func):
        self._event_func = event_func

class JabberAuthenticator(GabbleAuthenticator):
    "Trivial XML stream authenticator that accepts one username/digest pair."

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

    def __init__(self, username, password, resource=None, emit_events=False):
        GabbleAuthenticator.__init__(self, username, password, resource)
        self.emit_events = emit_events

    def streamStarted(self, root=None):
        if root:
            self.xmlstream.sid = '%x' % random.randint(1, sys.maxint)

        self.xmlstream.sendHeader()
        self.xmlstream.addOnetimeObserver(
            "/iq/query[@xmlns='jabber:iq:auth']", self.initialIq)

    def initialIq(self, iq):
        if self.emit_events:
            self._event_func(Event('auth-initial-iq', authenticator=self,
                iq=iq, id=iq["id"]))
        else:
            self.respondToInitialIq(iq)

        self.xmlstream.addOnetimeObserver('/iq/query/username', self.secondIq)

    def respondToInitialIq(self, iq):
        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        query = result.addElement('query')
        query["xmlns"] = "jabber:iq:auth"
        query.addElement('username', content='test')
        query.addElement('password')
        query.addElement('digest')
        query.addElement('resource')
        self.xmlstream.send(result)

    def secondIq(self, iq):
        if self.emit_events:
            self._event_func(Event('auth-second-iq', authenticator=self,
                iq=iq, id=iq["id"]))
        else:
            self.respondToSecondIq(self, iq)

    def respondToSecondIq(self, iq):
        username = xpath.queryForNodes('/iq/query/username', iq)
        assert map(str, username) == [self.username]

        digest = xpath.queryForNodes('/iq/query/digest', iq)
        expect = hashlib.sha1(self.xmlstream.sid + self.password).hexdigest()
        assert map(str, digest) == [expect]

        resource = xpath.queryForNodes('/iq/query/resource', iq)
        assertLength(1, resource)
        if self.resource is not None:
            assertEquals(self.resource, str(resource[0]))

        self.bare_jid = '%s@localhost' % self.username
        self.full_jid = '%s/%s' % (self.bare_jid, resource)

        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        self.xmlstream.send(result)
        self.xmlstream.dispatch(self.xmlstream, xmlstream.STREAM_AUTHD_EVENT)

class XmppAuthenticator(GabbleAuthenticator):
    def __init__(self, username, password, resource=None):
        GabbleAuthenticator.__init__(self, username, password, resource)
        self.authenticated = False

    def streamInitialize(self, root):
        if root:
            self.xmlstream.sid = root.getAttribute('id')

        if self.xmlstream.sid is None:
            self.xmlstream.sid = '%x' % random.randint(1, sys.maxint)

        self.xmlstream.sendHeader()

    def streamIQ(self):
        features = elem(xmlstream.NS_STREAMS, 'features')(
            elem(ns.NS_XMPP_BIND, 'bind'),
            elem(ns.NS_XMPP_SESSION, 'session'),
        )
        self.xmlstream.send(features)

        self.xmlstream.addOnetimeObserver(
            "/iq/bind[@xmlns='%s']" % ns.NS_XMPP_BIND, self.bindIq)
        self.xmlstream.addOnetimeObserver(
            "/iq/session[@xmlns='%s']" % ns.NS_XMPP_SESSION, self.sessionIq)

    def streamSASL(self):
        features = domish.Element((xmlstream.NS_STREAMS, 'features'))
        mechanisms = features.addElement((ns.NS_XMPP_SASL, 'mechanisms'))
        mechanism = mechanisms.addElement('mechanism', content='PLAIN')
        self.xmlstream.send(features)

        self.xmlstream.addOnetimeObserver("/auth", self.auth)

    def streamStarted(self, root=None):
        self.streamInitialize(root)

        if self.authenticated:
            # Initiator authenticated itself, and has started a new stream.
            self.streamIQ()
        else:
            self.streamSASL()

    def auth(self, auth):
        assert (base64.b64decode(str(auth)) ==
            '\x00%s\x00%s' % (self.username, self.password))

        success = domish.Element((ns.NS_XMPP_SASL, 'success'))
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
        bind = result.addElement((ns.NS_XMPP_BIND, 'bind'))
        self.bare_jid = '%s@localhost' % self.username
        self.full_jid = '%s/%s' % (self.bare_jid, resource)
        jid = bind.addElement('jid', content=self.full_jid)
        self.xmlstream.send(result)

        self.xmlstream.dispatch(self.xmlstream, xmlstream.STREAM_AUTHD_EVENT)

    def sessionIq(self, iq):
        self.xmlstream.send(make_result_iq(self.xmlstream, iq))

def make_stream_event(type, stanza, stream):
    event = servicetest.Event(type, stanza=stanza)
    event.stream = stream
    event.to = stanza.getAttribute("to")
    return event

def make_iq_event(stream, iq):
    event = make_stream_event('stream-iq', iq, stream)
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

def make_presence_event(stream, stanza):
    event = make_stream_event('stream-presence', stanza, stream)
    event.presence_type = stanza.getAttribute('type')

    statuses = xpath.queryForNodes('/presence/status', stanza)

    if statuses:
        event.presence_status = str(statuses[0])

    return event

def make_message_event(stream, stanza):
    event = make_stream_event('stream-message', stanza, stream)
    event.message_type = stanza.getAttribute('type')
    return event

class StreamFactory(twisted.internet.protocol.Factory):
    def __init__(self, streams, jids):
        self.streams = streams
        self.jids = jids
        self.presences = {}
        self.mappings = dict(map (lambda jid, stream: (jid, stream),
                                  jids, streams))

        # Make a copy of the streams
        self.factory_streams = list(streams)
        self.factory_streams.reverse()

        # Do not add observers for single instances because it's unnecessary and
        # some unit tests need to respond to the roster request, and we shouldn't
        # answer it for them otherwise we break compatibility
        if len(streams) > 1:
            # We need to have a function here because lambda keeps a reference on
            # the stream and jid and in the for loop, there is no context
            def addObservers(stream, jid):
                stream.addObserver('/iq', lambda x: \
                                       self.forward_iq(stream, jid, x))
                stream.addObserver('/presence', lambda x: \
                                       self.got_presence(stream, jid, x))

            for (jid, stream) in self.mappings.items():
                addObservers(stream, jid)

    def protocol(self, *args):
        return self.factory_streams.pop()


    def got_presence (self, stream, jid, stanza):
        stanza.attributes['from'] = jid
        self.presences[jid] = stanza

        for dest_jid  in self.presences.keys():
            # Dispatch the new presence to other clients
            stanza.attributes['to'] = dest_jid
            self.mappings[dest_jid].send(stanza)

            # Don't echo the presence twice
            if dest_jid != jid:
                # Dispatch other client's presence to this stream
                presence = self.presences[dest_jid]
                presence.attributes['to'] = jid
                stream.send(presence)

    def lost_presence(self, stream, jid):
        if self.presences.has_key(jid):
            del self.presences[jid]
            for dest_jid  in self.presences.keys():
                presence = domish.Element(('jabber:client', 'presence'))
                presence['from'] = jid
                presence['to'] = dest_jid
                presence['type'] = 'unavailable'
                self.mappings[dest_jid].send(presence)

    def forward_iq(self, stream, jid, stanza):
        stanza.attributes['from'] = jid

        query = stanza.firstChildElement()

        # Fake other accounts as being part of our roster
        if query and query.uri == ns.ROSTER:
            roster = make_result_iq(stream, stanza)
            query = roster.firstChildElement()
            for roster_jid in self.mappings.keys():
                if jid != roster_jid:
                    item = query.addElement('item')
                    item['jid'] = roster_jid
                    item['subscription'] = 'both'
            stream.send(roster)
            return

        to = stanza.getAttribute('to')
        dest = None
        if to is not None:
            dest = self.mappings.get(to)

        if dest is not None:
            dest.send(stanza)

class BaseXmlStream(xmlstream.XmlStream):
    initiating = False
    namespace = 'jabber:client'
    pep_support = True
    disco_features = []
    handle_privacy_lists = True

    def __init__(self, event_func, authenticator):
        xmlstream.XmlStream.__init__(self, authenticator)
        self.event_func = event_func
        self.addObserver('//iq', lambda x: event_func(
            make_iq_event(self, x)))
        self.addObserver('//message', lambda x: event_func(
            make_message_event(self, x)))
        self.addObserver('//presence', lambda x: event_func(
            make_presence_event(self, x)))
        self.addObserver('//event/stream/authd', self._cb_authd)
        if self.handle_privacy_lists:
            self.addObserver("/iq/query[@xmlns='%s']" % ns.PRIVACY,
                             self._cb_priv_list)

    def _cb_priv_list(self, iq):
        send_error_reply(self, iq)

    def _cb_authd(self, _):
        # called when stream is authenticated
        assert self.authenticator.full_jid is not None
        assert self.authenticator.bare_jid is not None

        self.addObserver(
            "/iq[@to='localhost']/query[@xmlns='http://jabber.org/protocol/disco#info']",
            self._cb_disco_iq)
        self.addObserver(
            "/iq[@to='%s']/query[@xmlns='http://jabber.org/protocol/disco#info']"
                % self.authenticator.bare_jid,
            self._cb_bare_jid_disco_iq)
        self.event_func(servicetest.Event('stream-authenticated'))

    def _cb_disco_iq(self, iq):
        nodes = xpath.queryForNodes(
            "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']", iq)
        query = nodes[0]

        for feature in self.disco_features:
            query.addChild(elem('feature', var=feature))

        iq['type'] = 'result'
        iq['from'] = iq['to']
        self.send(iq)

    def _cb_bare_jid_disco_iq(self, iq):
        # advertise PEP support
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

    def onDocumentEnd(self):
        self.event_func(servicetest.Event('stream-closed'))
        # We don't chain up XmlStream.onDocumentEnd() because it will
        # disconnect the TCP connection making tests as
        # connect/disconnect-timeout.py not working

    def send_stream_error(self, error='system-shutdown'):
        # Yes, there are meant to be two different STREAMS namespaces.
        go_away = \
            elem(xmlstream.NS_STREAMS, 'error')(
                elem(ns.STREAMS, error)
            )

        self.send(go_away)

class JabberXmlStream(BaseXmlStream):
    version = (0, 9)

class XmppXmlStream(BaseXmlStream):
    version = (1, 0)

class GoogleXmlStream(BaseXmlStream):
    version = (1, 0)

    pep_support = False
    disco_features = [ns.GOOGLE_ROSTER,
                      ns.GOOGLE_JINGLE_INFO,
                      ns.GOOGLE_MAIL_NOTIFY,
                      ns.GOOGLE_QUEUE,
                     ]

    def _cb_bare_jid_disco_iq(self, iq):
        # Google talk doesn't support PEP :(
        iq['type'] = 'result'
        iq['from'] = iq['to']
        self.send(iq)


def make_connection(bus, event_func, params=None, suffix=''):
    # Gabble accepts a resource in 'account', but the value of 'resource'
    # overrides it if there is one.
    test_name = re.sub('(.*tests/twisted/|\./)', '',  sys.argv[0])
    account = 'test%s@localhost/%s' % (suffix, test_name)

    default_params = {
        'account': account,
        'password': 'pass',
        'resource': 'Resource',
        'server': 'localhost',
        'port': dbus.UInt32(4242),
        'fallback-socks5-proxies': dbus.Array([], signature='s'),
        }

    if params:
        default_params.update(params)

     # Allow omitting the 'password' param
    if default_params['password'] is None:
        del default_params['password']

     # Allow omitting the 'account' param
    if default_params['account'] is None:
        del default_params['account']

    jid = default_params.get('account', None)
    conn =  servicetest.make_connection(bus, event_func, 'gabble', 'jabber',
                                        default_params)
    return (conn, jid)

def make_stream(event_func, authenticator=None, protocol=None,
                resource=None, suffix=''):
    # set up Jabber server
    if authenticator is None:
        authenticator = XmppAuthenticator('test%s' % suffix, 'pass', resource=resource)

    authenticator.set_event_func(event_func)

    if protocol is None:
        protocol = XmppXmlStream

    stream = protocol(event_func, authenticator)
    return stream

def disconnect_conn(q, conn, stream, expected_before=[], expected_after=[]):
    call_async(q, conn, 'Disconnect')

    tmp = expected_before + [
        EventPattern('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-closed')]

    before_events = q.expect_many(*tmp)

    stream.sendFooter()

    tmp = expected_after + [EventPattern('dbus-return', method='Disconnect')]
    after_events = q.expect_many(*tmp)

    return before_events[:-2], after_events[:-1]

def exec_test_deferred(fun, params, protocol=None, timeout=None,
                        authenticator=None, num_instances=1,
                        do_connect=True):
    # hack to ease debugging
    domish.Element.__repr__ = domish.Element.toXml
    colourer = None

    if sys.stdout.isatty() or 'CHECK_FORCE_COLOR' in os.environ:
        colourer = servicetest.install_colourer()

    bus = dbus.SessionBus()

    queue = servicetest.IteratingEventQueue(timeout)
    queue.verbose = (
        os.environ.get('CHECK_TWISTED_VERBOSE', '') != ''
        or '-v' in sys.argv)

    conns = []
    jids = []
    streams = []
    resource = params.get('resource') if params is not None else None
    for i in range(0, num_instances):
        if i == 0:
            suffix = ''
        else:
            suffix = str(i)

        try:
            (conn, jid) = make_connection(bus, queue.append, params, suffix)
        except Exception, e:
            # Crap. This is normally because the connection's still kicking
            # around on the bus. Let's bin any connections we *did* manage to
            # get going and then bail out unceremoniously.
            print e

            for conn in conns:
                conn.Disconnect()

            os._exit(1)

        conns.append(conn)
        jids.append(jid)
        streams.append(make_stream(queue.append, protocol=protocol,
                                   authenticator=authenticator,
                                   resource=resource, suffix=suffix))

    factory = StreamFactory(streams, jids)
    port = reactor.listenTCP(4242, factory)

    def signal_receiver(*args, **kw):
        if kw['path'] == '/org/freedesktop/DBus' and \
                kw['member'] == 'NameOwnerChanged':
            bus_name, old_name, new_name = args
            if new_name == '':
                for i, conn in enumerate(conns):
                    stream = streams[i]
                    jid = jids[i]
                    if conn._requested_bus_name == bus_name:
                        factory.lost_presence(stream, jid)
                        break
        queue.append(Event('dbus-signal',
                           path=unwrap(kw['path']),
                           signal=kw['member'], args=map(unwrap, args),
                           interface=kw['interface']))

    match_all_signals = bus.add_signal_receiver(
        signal_receiver,
        None,       # signal name
        None,       # interface
        None,
        path_keyword='path',
        member_keyword='member',
        interface_keyword='interface',
        byte_arrays=True
        )

    error = None

    try:
        if do_connect:
            for conn in conns:
                conn.Connect()
                queue.expect('dbus-signal', signal='StatusChanged',
                    args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])
                queue.expect('stream-authenticated')
                queue.expect('dbus-signal', signal='PresenceUpdate',
                    args=[{1L: (0L, {u'available': {}})}])
                queue.expect('dbus-signal', signal='StatusChanged',
                    args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

        if len(conns) == 1:
            fun(queue, bus, conns[0], streams[0])
        else:
            fun(queue, bus, conns, streams)
    except Exception, e:
        traceback.print_exc()
        error = e
        queue.verbose = False

    if colourer:
        sys.stdout = colourer.fh

    d = port.stopListening()

    # Does the Connection object still exist?
    for i, conn in enumerate(conns):
        if not bus.name_has_owner(conn.object.bus_name):
            # Connection has already been disconnected and destroyed
            continue
        try:
            if conn.GetStatus() == cs.CONN_STATUS_CONNECTED:
                # Connection is connected, properly disconnect it
                disconnect_conn(queue, conn, streams[i])
            else:
                # Connection is not connected, call Disconnect() to destroy it
                conn.Disconnect()
        except dbus.DBusException, e:
            pass
        except Exception, e:
            traceback.print_exc()
            error = e

        try:
            conn.Disconnect()
            raise AssertionError("Connection didn't disappear; "
                "all subsequent tests will probably fail")
        except dbus.DBusException, e:
            pass
        except Exception, e:
            traceback.print_exc()
            error = e

    match_all_signals.remove()

    if error is None:
        d.addBoth((lambda *args: reactor.crash()))
    else:
        # please ignore the POSIX behind the curtain
        d.addBoth((lambda *args: os._exit(1)))


def exec_test(fun, params=None, protocol=None, timeout=None,
              authenticator=None, num_instances=1, do_connect=True):
    reactor.callWhenRunning(
        exec_test_deferred, fun, params, protocol, timeout, authenticator, num_instances,
        do_connect)
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

def elem(a, b=None, attrs={}, **kw):
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
    >>> elem('foo', attrs={'xmlns:bar': 'urn:bar', 'bar:cake': 'yum'})(
    ...   elem('bar:e')(u'i')
    ... ).toXml()
    u"<foo xmlns:bar='urn:bar' bar:cake='yum'><bar:e>i</bar:e></foo>"
    """

    class _elem(domish.Element):
        def __call__(self, *children):
            _elem_add(self, *children)
            return self

    if b is not None:
        elem = _elem((a, b))
    else:
        elem = _elem((None, a))

    # Can't just update kw into attrs, because that *modifies the parameter's
    # default*. Thanks python.
    allattrs = {}
    allattrs.update(kw)
    allattrs.update(attrs)

    # First, let's pull namespaces out
    realattrs = {}
    for k, v in allattrs.iteritems():
        if k.startswith('xmlns:'):
            abbr = k[len('xmlns:'):]
            elem.localPrefixes[abbr] = v
        else:
            realattrs[k] = v

    for k, v in realattrs.iteritems():
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
