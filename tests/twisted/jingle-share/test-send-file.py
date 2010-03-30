# -*- coding: utf-8 -*-
from file_transfer_helper import SendFileTest, ReceiveFileTest, \
    exec_file_transfer_test, File

if __name__ == '__main__':
    file = File()
    file.offset = 5
    file.name = "The greek foo δοκιμή.txt"
    exec_file_transfer_test(SendFileTest, ReceiveFileTest, file)
