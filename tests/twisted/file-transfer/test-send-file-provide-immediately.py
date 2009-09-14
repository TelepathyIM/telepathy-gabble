import dbus
import constants as cs
from file_transfer_helper import SendFileTest, exec_file_transfer_test

class SendFileTransferProvideImmediately(SendFileTest):
    def provide_file(self):

        # try to accept our outgoing file transfer
        try:
            self.ft_channel.AcceptFile(self.address_type,
                self.access_control, self.access_control_param, self.file.offset,
                byte_arrays=True)
        except dbus.DBusException, e:
            assert e.get_dbus_name() == cs.NOT_AVAILABLE
        else:
            assert False

        SendFileTest.provide_file(self)

        # state is still Pending as remote didn't accept the transfer yet
        state = self.ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'State')
        assert state == cs.FT_STATE_PENDING

    def client_accept_file(self):
        SendFileTest.client_accept_file(self)

        e = self.q.expect('dbus-signal', signal='InitialOffsetDefined')
        offset = e.args[0]
        assert offset == self.file.offset

        # Channel is open. We can start to send the file
        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == cs.FT_STATE_OPEN
        assert reason == cs.FT_STATE_CHANGE_REASON_NONE

if __name__ == '__main__':
    exec_file_transfer_test(SendFileTransferProvideImmediately)
