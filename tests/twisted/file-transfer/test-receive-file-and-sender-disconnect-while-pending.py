import dbus

from twisted.words.xish import domish

from gabbletest import exec_test
from file_transfer_helper import ReceiveFileTest, SOCKET_ADDRESS_TYPE_UNIX,\
    SOCKET_ACCESS_CONTROL_LOCALHOST, BytestreamIBB, BytestreamS5B

class ReceiveFileAndSenderDisconnectWhilePendingTest(ReceiveFileTest):
    def accept_file(self):
        # The sender of the file disconnects
        presence = domish.Element(('jabber:client', 'presence'))
        presence['from'] = self.contact_full_jid
        presence['to'] = 'test@localhost/Resource'
        presence['type'] = 'unavailable'
        self.stream.send(presence)

        self.q.expect('dbus-signal', signal='FileTransferStateChanged')

        # We can't accept the transfer now
        try:
            self.ft_channel.AcceptFile(SOCKET_ADDRESS_TYPE_UNIX,
                SOCKET_ACCESS_CONTROL_LOCALHOST, "", 0)
        except dbus.DBusException, e:
            assert e.get_dbus_name() == 'org.freedesktop.Telepathy.Errors.NotAvailable'
        else:
            assert False

        self.close_channel()

        # stop the test
        return True

if __name__ == '__main__':
    test = ReceiveFileAndSenderDisconnectWhilePendingTest(BytestreamIBB)
    exec_test(test.test)
