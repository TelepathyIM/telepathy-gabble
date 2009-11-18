"""
Test Gabble's implementation of sidecars, using the test plugin.
"""

from servicetest import call_async, EventPattern, assertEquals, assertContains
from gabbletest import exec_test
import constants as cs

TEST_PLUGIN_IFACE = "org.freedesktop.Telepathy.Gabble.Plugin.Test"

def test(q, bus, conn, stream):
    # Request a sidecar thate we support before we're connected; it should just
    # wait around until we're connected.
    call_async(q, conn.Future, 'EnsureSidecar', TEST_PLUGIN_IFACE)

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # Now we're connected, the call we made earlier should return.
    path, props = q.expect('dbus-return', method='EnsureSidecar').value
    # This sidecar doesn't even implement get_immutable_properties; it should
    # just get the empty dict filled in for it.
    assertEquals({}, props)

    # We should get the same sidecar if we request it again
    path2, props2 = conn.Future.EnsureSidecar(TEST_PLUGIN_IFACE)
    assertEquals((path, props), (path2, props2))

    # This is not a valid interface name
    call_async(q, conn.Future, 'EnsureSidecar', 'not an interface')
    q.expect('dbus-error', name=cs.INVALID_ARGUMENT)

    # The test plugin makes no reference to this interface.
    call_async(q, conn.Future, 'EnsureSidecar', 'unsupported.sidecar')
    q.expect('dbus-error', name=cs.NOT_IMPLEMENTED)

    # This sidecar does have some properties:
    path, props = conn.Future.EnsureSidecar(TEST_PLUGIN_IFACE + ".Props")
    assertContains(TEST_PLUGIN_IFACE + ".Props.Greeting", props)

    # The plugin claims it implements this sidecar, but actually doesn't. Check
    # that we don't blow up (although this is no different from Gabble's
    # perspective to creating a sidecar failing because a network service
    # wasn't there, for instance).
    call_async(q, conn.Future, 'EnsureSidecar', TEST_PLUGIN_IFACE + ".Buggy")
    q.expect('dbus-error', name=cs.NOT_IMPLEMENTED)

    # TODO: test ensuring a sidecar that waits for something from the network,
    # disconnecting while it's waiting, and ensuring that nothing breaks
    # regardless of whether the network replies before </stream:stream> or not.

    call_async(q, conn, 'Disconnect')

    q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-closed'),
        )

    call_async(q, conn.Future, 'EnsureSidecar', 'zomg.what')
    q.expect('dbus-error', name=cs.DISCONNECTED)

    stream.sendFooter()
    q.expect('dbus-return', method='Disconnect')

if __name__ == '__main__':
    exec_test(test)
