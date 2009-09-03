"""Send malformed pubsub notifications to be sure that Gabble isn't confused about those"""
from gabbletest import exec_test, elem, sync_stream

import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
      args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # event node without NS
    message = elem('message', from_='bob@foo.com')(
        elem('event')(
            elem('items', node=ns.GEOLOC)(
                elem('item', id='12345')(
                    elem(ns.GEOLOC, 'geoloc')(
                        elem ('country') (u'France'))))))
    stream.send(message)

    # event node with a wrong NS
    message = elem('message', from_='bob@foo.com')(
        elem('badger', 'event')(
            elem('items', node=ns.GEOLOC)(
                elem('item', id='12345')(
                    elem(ns.GEOLOC, 'geoloc')(
                        elem ('country') (u'France'))))))
    stream.send(message)

    # event node without 'from'
    message = elem('message')(
        elem((ns.PUBSUB_EVENT), 'event')(
            elem('items', node=ns.GEOLOC)(
                elem('item', id='12345')(
                    elem(ns.GEOLOC, 'geoloc')(
                        elem ('country') (u'France'))))))
    stream.send(message)

    # event node with an invalid 'from'
    message = elem('message', from_='aaaa')(
        elem((ns.PUBSUB_EVENT), 'event')(
            elem('items', node=ns.GEOLOC)(
                elem('item', id='12345')(
                    elem(ns.GEOLOC, 'geoloc')(
                        elem ('country') (u'France'))))))
    stream.send(message)

    # no items node
    message = elem('message', from_='bob@foo.com')(
        elem((ns.PUBSUB_EVENT), 'event')())
    stream.send(message)

    # no item node
    message = elem('message', from_='bob@foo.com')(
        elem((ns.PUBSUB_EVENT), 'event')(
            elem('items', node=ns.GEOLOC)()))
    stream.send(message)

    # item node doesn't have any child
    message = elem('message', from_='bob@foo.com')(
        elem((ns.PUBSUB_EVENT), 'event')(
            elem('items', node=ns.GEOLOC)(
                elem('item', id='12345')())))
    stream.send(message)

    # the child of the item node doesn't have a NS
    message = elem('message', from_='bob@foo.com')(
        elem((ns.PUBSUB_EVENT), 'event')(
            elem('items', node=ns.GEOLOC)(
                elem('item', id='12345')(
                    elem('geoloc')(
                        elem ('country') (u'France'))))))
    stream.send(message)

    # valid but unknown pubsub notification
    message = elem('message', from_='bob@foo.com')(
        elem((ns.PUBSUB_EVENT), 'event')(
            elem('items', node='http://www.badger.com')(
                elem('item', id='12345')(
                    elem('http://www.badger.com', 'badger')(
                        elem ('mushroom') (u'snake'))))))
    stream.send(message)

    sync_stream(q, stream)

if __name__ == '__main__':
    exec_test(test)
