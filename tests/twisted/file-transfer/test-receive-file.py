from gabbletest import exec_test
from file_transfer_helper import ReceiveFileTest, BytestreamIBB, BytestreamS5B

if __name__ == '__main__':
    test = ReceiveFileTest(BytestreamIBB)
    exec_test(test.test)
    test = ReceiveFileTest(BytestreamS5B)
    exec_test(test.test)
