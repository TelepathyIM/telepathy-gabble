import dbus
import constants as cs

from servicetest import EventPattern
from file_transfer_helper import SendFileTest, ReceiveFileTest, \
    FileTransferTest, exec_file_transfer_test

from config import FILE_TRANSFER_ENABLED

if not FILE_TRANSFER_ENABLED:
    print "NOTE: built with --disable-file-transfer"
    raise SystemExit(77)

class ReceiveFileStopped(ReceiveFileTest):

    def __init__(self, file, address_type, access_control, access_control_param):
        FileTransferTest.__init__(self, file, address_type, access_control,
                                  access_control_param)
        self._actions = [self.connect, self.set_ft_caps, None,

                         self.wait_for_ft_caps, None,

                         self.check_new_channel, None,

                         self.check_stopped, None,

                         self.close_channel, self.done]

    def check_stopped(self):
        state_event = self.q.expect ('dbus-signal',
                                        signal='FileTransferStateChanged',
                                        path=self.channel.object_path)

        state, reason = state_event.args
        assert state == cs.FT_STATE_CANCELLED
        assert reason == cs.FT_STATE_CHANGE_REASON_REMOTE_STOPPED

        # try to provide the file
        try:
            self.accept_file()
        except dbus.DBusException, e:
            assert e.get_dbus_name() == cs.NOT_AVAILABLE
        else:
            assert False




class SendFileAndClose (SendFileTest):
    def __init__(self, file, address_type,
                 access_control, acces_control_param):
        FileTransferTest.__init__(self, file, address_type,
                                  access_control, acces_control_param)

        self._actions = [self.connect, self.set_ft_caps,
                         self.check_ft_available, None,

                         self.wait_for_ft_caps, None,

                         self.request_ft_channel, self.provide_file, None,

                         self.close_and_check, None,

                         self.close_channel, self.done]

    def close_and_check(self):
        self.channel.Close()

        state_event, _ = self.q.expect_many(
            EventPattern('dbus-signal', signal='FileTransferStateChanged',
                         path=self.channel.object_path),
            EventPattern('dbus-signal', signal='Closed',
                         path=self.channel.object_path))

        state, reason = state_event.args
        assert state == cs.FT_STATE_CANCELLED
        assert reason == cs.FT_STATE_CHANGE_REASON_LOCAL_STOPPED

if __name__ == '__main__':
    exec_file_transfer_test(SendFileAndClose, ReceiveFileStopped)
