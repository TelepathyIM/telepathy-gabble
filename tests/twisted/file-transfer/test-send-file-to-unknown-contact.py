import dbus

import constants as cs
from file_transfer_helper import SendFileTest, HT_CONTACT, exec_file_transfer_test

class SendFileTransferToUnknownContactTest(SendFileTest):
    def __init__(self, bytestream_cls):
        SendFileTest.__init__(self, bytestream_cls)

        self._actions = [self.connect, self.check_ft_available, self.my_request_ft_channel]

    def my_request_ft_channel(self):
        self.contact_name = 'jean@localhost'
        self.handle = self.conn.RequestHandles(HT_CONTACT, [self.contact_name])[0]

        try:
            self.request_ft_channel()
        except dbus.DBusException, e:
            assert e.get_dbus_name() == cs.OFFLINE
        else:
            assert False

if __name__ == '__main__':
    exec_file_transfer_test(SendFileTransferToUnknownContactTest)
