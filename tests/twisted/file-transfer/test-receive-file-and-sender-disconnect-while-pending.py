import dbus

from twisted.words.xish import domish

import constants as cs
from file_transfer_helper import exec_file_transfer_test, ReceiveFileTest

class ReceiveFileAndSenderDisconnectWhilePendingTest(ReceiveFileTest):
    def accept_file(self):
        # The sender of the file disconnects
        presence = domish.Element(('jabber:client', 'presence'))
        presence['from'] = self.contact_full_jid
        presence['to'] = 'test@localhost/Resource'
        presence['type'] = 'unavailable'
        self.stream.send(presence)

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == cs.FT_STATE_CANCELLED
        assert reason == cs.FT_STATE_CHANGE_REASON_REMOTE_STOPPED

        # We can't accept the transfer now
        try:
            # IPv4 is otherwise guaranteed to be available
            self.ft_channel.AcceptFile(cs.SOCKET_ADDRESS_TYPE_IPV4,
                cs.SOCKET_ACCESS_CONTROL_LOCALHOST, "", 0)
        except dbus.DBusException, e:
            assert e.get_dbus_name() == cs.NOT_AVAILABLE
        else:
            assert False

        self.close_channel()

        # stop the test
        return True

if __name__ == '__main__':
    exec_file_transfer_test(ReceiveFileAndSenderDisconnectWhilePendingTest)
