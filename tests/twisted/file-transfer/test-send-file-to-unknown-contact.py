import dbus

import constants as cs
from file_transfer_helper import SendFileTest, exec_file_transfer_test

from config import FILE_TRANSFER_ENABLED

if not FILE_TRANSFER_ENABLED:
    print("NOTE: built with --disable-file-transfer")
    raise SystemExit(77)

class SendFileTransferToUnknownContactTest(SendFileTest):
    def __init__(self, bytestream_cls, file, address_type, access_control, acces_control_param):
        SendFileTest.__init__(self, bytestream_cls, file, address_type, access_control, acces_control_param)

        self._actions = [self.connect, self.check_ft_available, self.my_request_ft_channel]

    def my_request_ft_channel(self):
        self.contact_name = 'jean@localhost'
        self.handle = self.conn.get_contact_handle_sync(self.contact_name)

        try:
            self.request_ft_channel()
        except dbus.DBusException as e:
            assert e.get_dbus_name() == cs.OFFLINE
        else:
            assert False

if __name__ == '__main__':
    exec_file_transfer_test(SendFileTransferToUnknownContactTest)
