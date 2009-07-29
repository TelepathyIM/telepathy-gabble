from servicetest import call_async, EventPattern
from gabbletest import exec_test, acknowledge_iq, make_result_iq
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

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

    # deprecated version
    types, minw, minh, maxw, maxh, maxb = conn.Avatars.GetAvatarRequirements()
    assert types[0] == 'image/png', types
    assert 'image/jpeg' in types, types
    assert 'image/gif' in types, types
    assert minw == 32, minw
    assert minh == 32, minh
    assert maxw == 96, maxw
    assert maxh == 96, maxh
    assert maxb == 8192, maxb

if __name__ == '__main__':
    exec_test(test)
