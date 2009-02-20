from gabbletest import exec_test
from file_transfer_helper import ReceiveFileTestIBB

if __name__ == '__main__':
    test = ReceiveFileTestIBB()
    exec_test(test.test)
