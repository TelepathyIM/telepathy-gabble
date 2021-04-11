# coding=utf-8
"""
Test XEP-0333 markers.
"""

from servicetest import (
    EventPattern, assertEquals, assertLength, sync_dbus, wrap_channel,
)
from gabbletest import exec_test, elem, sync_stream, acknowledge_iq
import constants as cs
import ns

import caps_helper
import rostertest
from presence.invisible_helper import Xep0186Stream

CONTACT = 'guybrush@mi.lit'
CONTACT_FULL_JID = CONTACT + '/Sea Cucumber'

def send_received_report(stream, jid, received_id):
    stream.send(
        elem('message', from_=jid)(
          elem(ns.NS_CHAT_MARKERS, 'received', id=received_id)
        ))

def marker_received_on_open_channel(q, bus, conn, stream, chan):
    received_id = 'fine-leather-jackets'

    send_received_report(stream, CONTACT, received_id)
    e = q.expect('dbus-signal', signal='MessageReceived', path=chan.object_path)
    message, = e.args
    header, = message

    assertEquals(cs.MT_DELIVERY_REPORT, header['message-type'])
    assertEquals(cs.DELIVERY_STATUS_DELIVERED, header['delivery-status'])
    assertEquals(received_id, header['delivery-token'])

def marker_ignored_without_channel(q, bus, conn, stream):
    q.forbid_events([EventPattern('dbus-signal', signal='MessageReceived')])
    send_received_report(stream, 'marley@mi.gov', 'only-one-candidate')
    sync_dbus(bus, q, conn)
    q.unforbid_all()

def not_sending_request_to_contact(q, bus, conn, stream, chan):
    message = [
      { 'message-type': cs.MT_NORMAL,
      },
      { 'content-type': 'text/plain',
        'content': 'Mancomb Seepgood?',
      }]
    chan.Messages.SendMessage(message, 0)

    e = q.expect('stream-message', to=CONTACT)
    assertLength(0, list(e.stanza.elements(uri=ns.NS_CHAT_MARKERS, name='markable')))

def sending_request_to_presenceless_contact(q, bus, conn, stream, chan):
    """
    Initially we know nothing of Guybrush's presence, so should just try our
    level best if asked to.
    """
    message = [
      { 'message-type': cs.MT_NORMAL,
      },
      { 'content-type': 'text/plain',
        'content': 'Thriftweed?',
      }]
    chan.Messages.SendMessage(message, cs.MSG_SENDING_FLAGS_REPORT_READ)

    e = q.expect('stream-message', to=CONTACT)
    assertLength(1, list(e.stanza.elements(uri=ns.NS_CHAT_MARKERS, name='markable')))

def sending_request_to_cappy_contact(q, bus, conn, stream, chan):
    """
    Test that Gabble requests a marker from a contact whom we know supports
    this extension, but only if asked.
    """

    # Convince Gabble that Guybrush supports this extension
    caps = { 'node': 'http://whatever',
             # FIXME: should be markers, not receipt, see #23
             #'ver': caps_helper.compute_caps_hash([], [ns.NS_CHAT_MARKERS], {}),
             'ver': caps_helper.compute_caps_hash([], [ns.NS_CHAT_MARKERS, ns.RECEIPTS], {}),
             'hash': 'sha-1',
           }
    caps_helper.presence_and_disco(q, conn, stream, CONTACT_FULL_JID,
        disco=True,
        client=caps['node'],
        caps=caps,
        features=[ns.NS_CHAT_MARKERS, ns.RECEIPTS])
    sync_stream(q, stream)

    # Don't ask, don't tell — even if we know Guybrush does support this.
    not_sending_request_to_contact(q, bus, conn, stream, chan)

    # Ask, tell.
    message = [
      { 'message-type': cs.MT_NORMAL,
      },
      { 'content-type': 'text/plain',
        'content': 'Ulysses?',
      }]
    chan.Messages.SendMessage(message, cs.MSG_SENDING_FLAGS_REPORT_READ)

    e = q.expect('stream-message', to=CONTACT)
    assertLength(1, list(e.stanza.elements(uri=ns.NS_CHAT_MARKERS, name='markable')))

def replying_to_requests(q, bus, conn, stream):
    jid = 'lechuck@lucasarts.lit'

    # We shouldn't send markers to people who aren't on our roster.
    q.forbid_events([EventPattern('stream-message', to=jid)])
    stream.send(
        elem('message', from_=jid, type='chat', id='alpha')(
          elem('body')(
            u"You didn't kill me, you moron!"
          ),
          elem(ns.NS_CHAT_MARKERS, 'markable')
        ))

    event = q.expect('dbus-signal', signal='NewChannels')
    path, props = event.args[0][0]
    text_chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text')
    event = q.expect('dbus-signal', signal='MessageReceived')
    message = event.args[0]
    header, body = message
    message_id = header['pending-message-id']
    # FIXME: uncomment below once #23 is fixed
    #text_chan.Text.AcknowledgePendingMessages([message_id])

    sync_stream(q, stream)
    q.unforbid_all()

    # We should send markers to people on our roster, seeing as we're not
    # invisible.
    rostertest.send_roster_push(stream, jid, subscription='from')

    stream.send(
        elem('message', from_=jid, type='chat', id='beta')(
          elem('body')(
            u"You've just destroyed my spiritual essences."
          ),
          elem(ns.NS_CHAT_MARKERS, 'markable')
        ))
    event = q.expect('dbus-signal', signal='MessageReceived')
    message = event.args[0]
    header, body = message
    message_id = header['pending-message-id']
    text_chan.Text.AcknowledgePendingMessages([message_id])

    e = q.expect('stream-message', to=jid)
    receipt = next(e.stanza.elements(uri=ns.NS_CHAT_MARKERS, name='displayed'))
    assertEquals('beta', receipt['id'])
    # FIXME: there will be alpha now coming as well, as when we acked recent it acks previous
    q.expect('stream-message', to=jid)

    # We would like requests in messages without id=''s not to crash Gabble,
    # and also for it not to send a reply.
    q.forbid_events([EventPattern('stream-message', to=jid)])
    stream.send(
        elem('message', from_=jid, type='chat')( # NB. no id='' attribute
          elem('body')(
            u"A favor that I shall now return!"
          ),
          elem(ns.NS_CHAT_MARKERS, 'markable')
        ))
    event = q.expect('dbus-signal', signal='MessageReceived')
    message = event.args[0]
    header, body = message
    message_id = header['pending-message-id']
    text_chan.Text.AcknowledgePendingMessages([message_id])

    sync_stream(q, stream)
    q.unforbid_all()

    # If we're invisible, LeChuck shouldn't get markers.
    conn.SimplePresence.SetPresence("hidden", "")
    event = q.expect('stream-iq', query_name='invisible')
    acknowledge_iq(stream, event.stanza)

    q.forbid_events([EventPattern('stream-message', to=jid)])
    stream.send(
        elem('message', from_=jid, type='chat', id='epsilon')(
          elem('body')(
            u"… but where am I going to find a duck wearing burlap chaps?"
          ),
          elem(ns.NS_CHAT_MARKERS, 'markable')
        ))
    event = q.expect('dbus-signal', signal='MessageReceived')
    message = event.args[0]
    header, body = message
    message_id = header['pending-message-id']
    # FIXME: same as at the beginning - uncomment when fixed
    #text_chan.Text.AcknowledgePendingMessages([message_id])

    sync_stream(q, stream)
    q.unforbid_all()

def test(q, bus, conn, stream):
    path = conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_ID: CONTACT,
              })[0]
    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text')

    # Let's start out with an empty roster, eh?
    e = q.expect('stream-iq', iq_type='get', query_ns=ns.ROSTER)
    e.stanza['type'] = 'result'
    stream.send(e.stanza)

    marker_received_on_open_channel(q, bus, conn, stream, chan)

    # FIXME: not implemented because of partial implementation
    # See https://github.com/TelepathyIM/telepathy-gabble/issues/23 for details
    #report_ignored_without_channel(q, bus, conn, stream)

    not_sending_request_to_contact(q, bus, conn, stream, chan)

    # FIXME: it still relies on receipts support which is still unable to
    # identify whether message's contact is unknown or has no required cap
    #sending_request_to_presenceless_contact(q, bus, conn, stream, chan)

    sending_request_to_cappy_contact(q, bus, conn, stream, chan)

    replying_to_requests(q, bus, conn, stream)


if __name__ == '__main__':
    params = {'send-chat-markers':True}
    exec_test(test, params=params, protocol=Xep0186Stream)
