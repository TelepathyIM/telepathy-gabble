import constants as cs
from file_transfer_helper import SendFileTest, exec_file_transfer_test

class SendFileTransferWaitToProvideTest(SendFileTest):
    def __init__(self, bytestream_cls, file, address_type, access_control, acces_control_param):
        SendFileTest.__init__(self, bytestream_cls, file, address_type, access_control, acces_control_param)

        self._actions =  [self.connect, self.check_ft_available, self.announce_contact,
            self.check_ft_available, self.request_ft_channel, self.create_ft_channel, self.got_send_iq,
            self.client_accept_file, self.provide_file, self.send_file, self.close_channel]

    def client_accept_file(self):
        # state is still Pending as remote didn't accept the transfer yet
        state = self.ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'State')
        assert state == cs.FT_STATE_PENDING

        SendFileTest.client_accept_file(self)

        # Remote accepted the transfer
        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == cs.FT_STATE_ACCEPTED, state
        assert reason == cs.FT_STATE_CHANGE_REASON_NONE

    def provide_file(self):
        SendFileTest.provide_file(self)

        e = self.q.expect('dbus-signal', signal='InitialOffsetDefined')
        offset = e.args[0]
        assert offset == self.file.offset

        # Channel is open. We can start to send the file
        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == cs.FT_STATE_OPEN
        assert reason == cs.FT_STATE_CHANGE_REASON_REQUESTED

if __name__ == '__main__':
    exec_file_transfer_test(SendFileTransferWaitToProvideTest)
