from twisted.words.xish import domish

import constants as cs
from file_transfer_helper import exec_file_transfer_test, ReceiveFileTest

class ReceiveFileAndSenderDisconnectWhileTransfering(ReceiveFileTest):
    def accept_file(self):
        ReceiveFileTest.accept_file(self)

        presence = domish.Element(('jabber:client', 'presence'))
        presence['from'] = self.contact_full_jid
        presence['to'] = 'test@localhost/Resource'
        presence['type'] = 'unavailable'
        self.stream.send(presence)

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == cs.FT_STATE_CANCELLED
        assert reason == cs.FT_STATE_CHANGE_REASON_REMOTE_STOPPED

        return True

if __name__ == '__main__':
    exec_file_transfer_test(ReceiveFileAndSenderDisconnectWhileTransfering)
