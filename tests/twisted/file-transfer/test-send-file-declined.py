from file_transfer_helper import SendFileTest, FT_STATE_CANCELLED, \
    FT_STATE_CHANGE_REASON_REMOTE_STOPPED, CHANNEL_TYPE_FILE_TRANSFER, exec_file_transfer_test

from twisted.words.xish import domish
import ns

class SendFileDeclinedTest(SendFileTest):
    def got_send_iq(self):
        SendFileTest.got_send_iq(self)

        # Receiver declines the file offer
        reply = domish.Element(('', 'iq'))
        reply['to'] = 'test@localhost/Resource'
        reply['from'] = self.iq['to']
        reply['type'] = 'error'
        reply['id'] = self.iq['id']
        error = reply.addElement((None, 'error'))
        error['code'] = '403'
        error['type'] = 'cancel'
        forbidden = error.addElement((ns.STANZA, 'forbidden'))
        text = error.addElement((ns.STANZA, 'text'), content='Offer Declined')
        self.stream.send(reply)

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == FT_STATE_CANCELLED, state
        assert reason == FT_STATE_CHANGE_REASON_REMOTE_STOPPED

        transferred = self.ft_props.Get(CHANNEL_TYPE_FILE_TRANSFER, 'TransferredBytes')
        # no byte has been transferred as the file was declined
        assert transferred == 0

        # stop test
        return True

if __name__ == '__main__':
    exec_file_transfer_test(SendFileDeclinedTest)
