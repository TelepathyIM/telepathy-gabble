from file_transfer_helper import SendFileTest, CHANNEL_TYPE_FILE_TRANSFER, FT_STATE_PENDING, \
    FT_STATE_CHANGE_REASON_NONE, FT_STATE_OPEN, BytestreamIBB, BytestreamS5B, exec_file_transfer_test

class SendFileTransferProvideImmediately(SendFileTest):
    def provide_file(self):
        SendFileTest.provide_file(self)

        # state is still Pending as remote didn't accept the transfer yet
        state = self.ft_props.Get(CHANNEL_TYPE_FILE_TRANSFER, 'State')
        assert state == FT_STATE_PENDING

    def client_accept_file(self):
        SendFileTest.client_accept_file(self)

        e = self.q.expect('dbus-signal', signal='InitialOffsetDefined')
        offset = e.args[0]
        # We don't support resume
        assert offset == 0

        # Channel is open. We can start to send the file
        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == FT_STATE_OPEN
        assert reason == FT_STATE_CHANGE_REASON_NONE

if __name__ == '__main__':
    exec_file_transfer_test(SendFileTransferProvideImmediately)
