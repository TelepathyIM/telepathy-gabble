from twisted.words.xish import domish

from file_transfer_helper import exec_file_transfer_test, ReceiveFileTest,\
    FT_STATE_CANCELLED, FT_STATE_CHANGE_REASON_REMOTE_STOPPED

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
        assert state == FT_STATE_CANCELLED
        assert reason == FT_STATE_CHANGE_REASON_REMOTE_STOPPED

        return True

if __name__ == '__main__':
    exec_file_transfer_test(ReceiveFileAndSenderDisconnectWhileTransfering)
