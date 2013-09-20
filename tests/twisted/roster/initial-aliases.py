"""
Test retrieving the aliases after connection.
"""

from servicetest import (
    assertContains, assertLength
    )
from gabbletest import (
    exec_test, make_result_iq
    )
import constants as cs
import ns

def add_contact(iq, jid, alias):
    query = iq.firstChildElement()
    item = query.addElement('item')
    item['jid'] = jid
    item['name'] = alias
    item['subscription'] = 'both'

def test(q, bus, conn, stream):
    """
    Check that when we receive a roster update we emit a single AliasesChanged
    for all the contacts.
    """

    conn.Connect()

    # Gabble asks the server for the initial roster
    event = q.expect('stream-iq', iq_type='get', query_ns=ns.ROSTER)
    result = make_result_iq(stream, event.stanza)

    # We reply with a roster with two contacts
    bob_jid = 'bob.smith@example.com'
    bob_alias = 'Bob Smith'
    add_contact(result, bob_jid, bob_alias)

    alice_jid = 'alice@example.com'
    alice_alias = 'Alice'
    add_contact(result, alice_jid, alice_alias)

    stream.send(result)

    # Check we get a single AliasesChanged for both contacts
    event = q.expect('dbus-signal', signal='AliasesChanged')
    added = event.args[0]

    bob_handle, alice_handle = conn.get_contact_handles_sync(
            [bob_jid, alice_jid])

    assertLength(2, added)
    assertContains((bob_handle, bob_alias), added)
    assertContains((alice_handle, alice_alias), added)

if __name__ == '__main__':
    exec_test(test, do_connect=False)
