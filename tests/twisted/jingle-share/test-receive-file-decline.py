import dbus
import constants as cs

from servicetest import EventPattern
from file_transfer_helper import SendFileTest, ReceiveFileTest, \
    FileTransferTest, exec_file_transfer_test

class ReceiveFileDecline(ReceiveFileTest):

    def __init__(self, file, address_type, access_control, access_control_param):
        FileTransferTest.__init__(self, file, address_type, access_control,
                                  access_control_param)
        self._actions = [self.connect, self.set_ft_caps, None,

                         self.wait_for_ft_caps, None,

                         self.check_new_channel, self.create_ft_channel,
                         self.close_and_check, self.done]

    def close_and_check(self):
        self.channel.Close()

        state_event, event, _ = self.q.expect_many(
            EventPattern('dbus-signal', signal='FileTransferStateChanged',
                         path=self.channel.__dbus_object_path__),
            EventPattern('stream-iq', stream=self.stream,
                         iq_type='set', query_name='session'),
            EventPattern('dbus-signal', signal='Closed',
                         path=self.channel.__dbus_object_path__))

        state, reason = state_event.args
        assert state == cs.FT_STATE_CANCELLED
        assert reason == cs.FT_STATE_CHANGE_REASON_LOCAL_STOPPED

        while event.query.getAttribute('type') != 'terminate':
            event = self.q.expect('stream-iq', stream=self.stream,
                                  iq_type='set', query_name='session')



class SendFileDeclined (SendFileTest):
    def __init__(self, file, address_type,
                 access_control, acces_control_param):
        FileTransferTest.__init__(self, file, address_type,
                                  access_control, acces_control_param)

        self._actions = [self.connect, self.set_ft_caps,
                         self.check_ft_available, None,

                         self.wait_for_ft_caps, None,

                         self.request_ft_channel, self.create_ft_channel,
                         self.provide_file, None,

                         self.check_declined, self.close_channel, self.done]

    def check_declined(self):
        state_event = self.q.expect('dbus-signal',
                                    signal='FileTransferStateChanged',
                                    path=self.channel.__dbus_object_path__)

        state, reason = state_event.args
        assert state == cs.FT_STATE_CANCELLED
        assert reason == cs.FT_STATE_CHANGE_REASON_REMOTE_STOPPED

        transferred = self.ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER,
                                        'TransferredBytes')
        # no byte has been transferred as the file was declined
        assert transferred == 0

        # try to provide the file
        try:
            self.provide_file()
        except dbus.DBusException, e:
            assert e.get_dbus_name() == cs.NOT_AVAILABLE
        else:
            assert False


if __name__ == '__main__':
    exec_file_transfer_test(SendFileDeclined, ReceiveFileDecline)
