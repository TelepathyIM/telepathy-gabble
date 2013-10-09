from gabbletest import exec_test
from servicetest import assertEquals
import constants as cs

def test_get_all(conn):
    props = conn.GetAll(cs.CONN_IFACE_AVATARS,
            dbus_interface=cs.PROPERTIES_IFACE)
    types = props['SupportedAvatarMIMETypes']
    minw = props['MinimumAvatarWidth']
    minh = props['MinimumAvatarHeight']
    maxw = props['MaximumAvatarWidth']
    maxh = props['MaximumAvatarHeight']
    maxb = props['MaximumAvatarBytes']
    rech = props['RecommendedAvatarHeight']
    recw = props['RecommendedAvatarWidth']

    assert types[0] == 'image/png', types
    assert 'image/jpeg' in types, types
    assert 'image/gif' in types, types
    assert minw == 32, minw
    assert minh == 32, minh
    assert maxw == 96, maxw
    assert maxh == 96, maxh
    assert maxb == 8192, maxb
    assert recw == 64, recw
    assert rech == 64, rech

def test(q, bus, conn, stream):
    test_get_all(conn)

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    test_get_all(conn)

    # deprecated version
    props = conn.Properties.GetAll(cs.CONN_IFACE_AVATARS)
    assertEquals(['image/png', 'image/jpeg', 'image/gif'], props['SupportedAvatarMIMETypes'])
    assertEquals(32, props['MinimumAvatarWidth'])
    assertEquals(32, props['MinimumAvatarHeight'])
    assertEquals(96, props['MaximumAvatarWidth'])
    assertEquals(96, props['MaximumAvatarHeight'])
    assertEquals(8192, props['MaximumAvatarBytes'])

if __name__ == '__main__':
    exec_test(test, do_connect=False)
