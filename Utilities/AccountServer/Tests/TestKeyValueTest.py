from xmlrpclib import ServerProxy
import unittest
import random
# This file contains a class specifying key value unit tests


class TestKeyValueTest(unittest.TestCase):
    """Tests operations defined in KeyValueTest.c against current build"""

    def setUp(self):
        """Initialize class variables"""
        self.proxy = ServerProxy("http://localhost:8081/xmlrpc")
        self.accounts = []
        self.accounts.append({
            "accountName" : "Bob",
            "uID" : 1,
            "uCreatedTime" : 11,
        })
        self.accounts.append( {
            "accountName" : "YoSoyLentGreen",
            "uID" : 2,
            "uCreatedTime" : 11,
        })
        dbrequest = {
            "Accounts" : self.accounts,
        }
        self.db_set = self.proxy.ReplaceAccountDatabase(dbrequest)
        request = {
            "AccountName" : "YoSoyLentGreen",
            "Key" : "TestPoints",
            "Value" : "9",
            "Increment" : 0,
        }
        self.proxy.SetKeyValueEx(request)

    def test_KVGet_success(self):
        """Tests KVGet method with valid parameters and a real account"""
        out = self.proxy.KVGet("YoSoyLentGreen", "TestPoints")
        self.assertEqual(out, 9)

    def test_KVRollback_success(self):
        """Tests KVRollback method with valid parameters and a real account"""
        out = self.proxy.KVRollback("YoSoyLentGreen", "TestPoints", "")
        self.assertEqual(0, out)

    def test_KVLock_success(self):
        """Tests KVLock method with valid parameters and a real account"""
        key = str(random.random()) + "KEY"
        s = self.proxy.KVLock("YoSoyLentGreen", key)
        self.assertNotEqual(s, "")


if __name__ == '__main__':
    unittest.main()
