import dbus
import constants as cs

from servicetest import EventPattern
from file_transfer_helper import SendFileTest, ReceiveFileTest, \
    FileTransferTest, exec_file_transfer_test

class SendFileAndWaitToProvide (SendFileTest):
    def __init__(self, file, address_type,
                 access_control, acces_control_param):
        FileTransferTest.__init__(self, file, address_type,
                                  access_control, acces_control_param)

        self._actions = [self.connect, self.set_ft_caps,
                         self.check_ft_available, None,

                         self.wait_for_ft_caps, None,

                         self.request_ft_channel, self.create_ft_channel,
                         self.check_pending_state, None,

                         self.check_accepted_state, self.provide_file,
                         self.send_file, self.wait_for_completion, None,

                         self.close_channel, self.done]

    def check_pending_state(self):
        # state is still Pending as remote didn't accept the transfer yet
        state = self.ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'State')
        assert state == cs.FT_STATE_PENDING

    def check_accepted_state(self):
        # Remote accepted the transfer
        state = self.ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'State')
        assert state == cs.FT_STATE_ACCEPTED, state

if __name__ == '__main__':
    exec_file_transfer_test(SendFileAndWaitToProvide, ReceiveFileTest)
