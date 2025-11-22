from xmlrpclib import ServerProxy
import sys
import unittest
# I don't know where the actual log is output yet.  
# No value given for "LogLine" seems to alter output of call.


class TestAccountLog(unittest.TestCase):
    """Tests XMLRPCAccountLog(struct) from XMLInterface.c"""
    
    def setUp(self):
        """Sets up globabl class variables"""
        self.prox = ServerProxy("http://localhost:8081/xmlrpc")
        self.test_unf_data = {
            "" : "user_not_found",
            "12" : "user_not_found",
            "ab" : "user_not_found",
            "1x2c3v4b" : "user_not_found",
            "09876543" : "user_not_found",
            "qazxswed" : "user_not_found",
            "tune_3" : "user_not_found",
            "4@TRAN" : "user_not_found",
            "@phish" : "user_not_found",
            "!@#$%^&*()_+~`" : "user_not_found",
            "Don't" : "user_not_found",
            "Stop Me Now" : "user_not_found",
        }
        self.test_success_data = {
            "MyFirstAccount" : "success",
        }
        
    def test_unf(self):
        """Tests with names of accounts which don't exist."""
        for key in self.test_unf_data.keys():
            request = {"AccountName" : key}
            out = self.prox.AccountLog(request)
            self.assertEqual(
                out["Status"],
                self.test_unf_data[key]
            )
            
    def test_success(self):
        """Tests with names of accounts which exist."""
        for key in self.test_success_data.keys():
            request = {"AccountName" : key}
            out = self.prox.AccountLog(request)
            self.assertEqual(
                out["Status"],
                self.test_success_data[key]
            )


if __name__ == '__main__':
    unittest.main()
