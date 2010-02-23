from file_transfer_helper import SendFileTest, ReceiveFileTest, \
    exec_file_transfer_test

if __name__ == '__main__':
    exec_file_transfer_test(SendFileTest, ReceiveFileTest)
