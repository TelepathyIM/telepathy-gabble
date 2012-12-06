# coding=utf-8
"""
Test XEP-0184 receipts.
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

GUYBRUSH = 'guybrush@mi.lit'
GUYBRUSH_FULL_JID = GUYBRUSH + '/Sea Cucumber'

def send_received_report(stream, jid, received_id):
    stream.send(
        elem('message', from_=jid)(
          elem(ns.RECEIPTS, 'received', id=received_id)
        ))

def report_received_on_open_channel(q, bus, conn, stream, chan):
    received_id = 'fine-leather-jackets'

    send_received_report(stream, GUYBRUSH, received_id)
    e = q.expect('dbus-signal', signal='MessageReceived', path=chan.object_path)
    message, = e.args
    header, = message

    assertEquals(cs.MT_DELIVERY_REPORT, header['message-type'])
    assertEquals(cs.DELIVERY_STATUS_DELIVERED, header['delivery-status'])
    assertEquals(received_id, header['delivery-token'])

def report_ignored_without_channel(q, bus, conn, stream):
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

    e = q.expect('stream-message', to=GUYBRUSH)
    assertLength(0, list(e.stanza.elements(uri=ns.RECEIPTS, name='request')))

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
    chan.Messages.SendMessage(message, cs.MSG_SENDING_FLAGS_REPORT_DELIVERY)

    e = q.expect('stream-message', to=GUYBRUSH)
    assertLength(1, list(e.stanza.elements(uri=ns.RECEIPTS, name='request')))

def sending_request_to_cappy_contact(q, bus, conn, stream, chan):
    """
    Test that Gabble requests a receipt from a contact whom we know supports
    this extension, but only if asked.
    """

    # Convince Gabble that Guybrush supports this extension
    caps = { 'node': 'http://whatever',
             'ver': caps_helper.compute_caps_hash([], [ns.RECEIPTS], {}),
             'hash': 'sha-1',
           }
    caps_helper.presence_and_disco(q, conn, stream, GUYBRUSH_FULL_JID,
        disco=True,
        client=caps['node'],
        caps=caps,
        features=[ns.RECEIPTS])
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
    chan.Messages.SendMessage(message, cs.MSG_SENDING_FLAGS_REPORT_DELIVERY)

    e = q.expect('stream-message', to=GUYBRUSH)
    assertLength(1, list(e.stanza.elements(uri=ns.RECEIPTS, name='request')))

def replying_to_requests(q, bus, conn, stream):
    jid = 'lechuck@lucasarts.lit'

    # We shouldn't send receipts to people who aren't on our roster.
    q.forbid_events([EventPattern('stream-message', to=jid)])
    stream.send(
        elem('message', from_=jid, type='chat', id='alpha')(
          elem('body')(
            u"You didn't kill me, you moron!"
          ),
          elem(ns.RECEIPTS, 'request')
        ))

    q.expect('dbus-signal', signal='MessageReceived')
    sync_stream(q, stream)
    q.unforbid_all()

    # We should send receipts to people on our roster, seeing as we're not
    # invisible.
    rostertest.send_roster_push(stream, jid, subscription='from')

    stream.send(
        elem('message', from_=jid, type='chat', id='beta')(
          elem('body')(
            u"You've just destroyed my spiritual essences."
          ),
          elem(ns.RECEIPTS, 'request')
        ))
    q.expect('dbus-signal', signal='MessageReceived')
    e = q.expect('stream-message', to=jid)
    receipt = e.stanza.elements(uri=ns.RECEIPTS, name='received').next()
    assertEquals('beta', receipt['id'])

    # We would like requests in messages without id=''s not to crash Gabble,
    # and also for it not to send a reply.
    q.forbid_events([EventPattern('stream-message', to=jid)])
    stream.send(
        elem('message', from_=jid, type='chat')( # NB. no id='' attribute
          elem('body')(
            u"A favor that I shall now return!"
          ),
          elem(ns.RECEIPTS, 'request')
        ))
    q.expect('dbus-signal', signal='MessageReceived')
    sync_stream(q, stream)
    q.unforbid_all()

    # If we're invisible, LeChuck shouldn't get receipts.
    conn.SimplePresence.SetPresence("hidden", "")
    event = q.expect('stream-iq', query_name='invisible')
    acknowledge_iq(stream, event.stanza)

    q.forbid_events([EventPattern('stream-message', to=jid)])
    stream.send(
        elem('message', from_=jid, type='chat', id='epsilon')(
          elem('body')(
            u"… but where am I going to find a duck wearing burlap chaps?"
          ),
          elem(ns.RECEIPTS, 'request')
        ))
    q.expect('dbus-signal', signal='MessageReceived')
    sync_stream(q, stream)
    q.unforbid_all()

def test(q, bus, conn, stream):
    path = conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_ID: GUYBRUSH,
              })[0]
    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text',
        ['Messages'])

    # Let's start out with an empty roster, eh?
    e = q.expect('stream-iq', iq_type='get', query_ns=ns.ROSTER)
    e.stanza['type'] = 'result'
    stream.send(e.stanza)

    report_received_on_open_channel(q, bus, conn, stream, chan)
    report_ignored_without_channel(q, bus, conn, stream)
    not_sending_request_to_contact(q, bus, conn, stream, chan)

    # FIXME: This test is disabled because of stupidity in the presence cache.
    # See the comment in receipts_conceivably_supported().
    #sending_request_to_presenceless_contact(q, bus, conn, stream, chan)

    sending_request_to_cappy_contact(q, bus, conn, stream, chan)

    replying_to_requests(q, bus, conn, stream)


if __name__ == '__main__':
    exec_test(test, protocol=Xep0186Stream)
