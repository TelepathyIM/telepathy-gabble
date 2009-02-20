from gabbletest import exec_test
from file_transfer_helper import ReceiveFileTestIBB, ReceiveFileTestS5B

if __name__ == '__main__':
    test = ReceiveFileTestIBB()
    exec_test(test.test)
    test = ReceiveFileTestS5B()
    exec_test(test.test)
