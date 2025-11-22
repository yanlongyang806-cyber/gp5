from xmlrpclib import ServerProxy
import unittest
import random
from TestVariables import *
# This file is a collection of classes.
# Each class contains the unit tests for a single xmlrpc method.
# These methods all relate to Key Values.
# You hear the Law and Order theme jingle playing in the distance.

def _request(acct, key, value, increment, reason, ty):
    """Helper method to initialize a request from input"""
    request = {}
    if acct is not "":
        request["AccountName"] = acct
    if key is not "":
        request["Key"] = key
    if request is not "":
        request["Value"] = str(value)
    if increment is not "":
        request["Increment"] = increment
    if key is not "":
        request["Reason"] = reason
    if ty is not "":
        request["Type"] = ty
    return request


class TestSetKeyValueEx(unittest.TestCase):
    """Tests XMLRPCSetOrChangeKeyValue from XMLInterface.c"""

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
        self.SET = 0
        self.INCREMENT = 1
        self.success_value = {
            "Result" : "key_set"
        }
        self.failure_value = {
            "Result" : "key_failure"
        }
        self.unf_value = {
            "Result" : "user_not_found"
        }
        self.invalid_range_value = {
            "Result" : "invalid_range"
        }
        for each in self.accounts:
            self.proxy.SetKeyValue(each["accountName"], "FooPoints",
                                           "9")
            self.proxy.SetKeyValue(each["accountName"], "BarPoints",
                                           "9")
        self.set_requests = []
        self.set_requests.append(_request("Bob", "FooPoints", "80",
                                          self.SET, "", 0))
        self.set_requests.append(_request("Bob", "FooPoints", "80",
                                          self.SET, "Unit Tests", ""))
        self.set_requests.append(_request("Bob", "FooPoints", "80", self.SET,
                                          "", ""))
        self.set_requests.append(_request("Bob", "FooPoints", "80",
                                          "", "Unit Tests", 0))
        self.set_requests.append(_request("Bob", "FooPoints", "80", "",
                                          "", 0))
        self.set_requests.append(_request("Bob", "FooPoints", "80", "",
                                          "Unit Tests", ""))
        self.set_requests.append(_request("Bob", "FooPoints", "80", "",
                                          "", ""))
        self.increment_requests = []
        self.increment_requests.append(_request("Bob", "FooPoints", "80", self.INCREMENT,
                                          "", 0))
        self.increment_requests.append(_request("Bob", "FooPoints", "80", self.INCREMENT,
                                          "Unit Tests", ""))
        self.increment_requests.append(_request("Bob", "FooPoints", "80", self.INCREMENT,
                                          "", ""))

    def test_setup(self):
        """Confirms database initialization. Not passing here invalidates
        account-based tests."""
        passing_SetUp = {
            "Result" : "success",
        }
        self.assertEqual(self.db_set, passing_SetUp)
        for each in self.set_requests:
            self.assertNotEqual(each, {})
        for each in self.increment_requests:
            self.assertNotEqual(each, {})
        for each in self.accounts:
            self.assertEqual(9, self.proxy.KVGet(each["accountName"], "FooPoints"))
            self.assertEqual(9, self.proxy.KVGet(each["accountName"], "BarPoints"))

    def test_increment_success(self):
        """Tests incrementing with existing accounts and valid parameters."""
        request = _request("Bob", "FooPoints", "80", 1,
                           "Success case unit test.", 0)
        outcome = {
            "Key" : "FooPoints",
            "Value" : "89",
        }
        out = self.proxy.SetKeyValueEx(request)
        info = self.proxy.UserInfoEx(
            {
            "AccountName" : "Bob",
            "Flags" : USERINFO_KEYVALUES,
            }
        )
        self.assertEqual(out, self.success_value)
        self.assertIn(outcome, info["Keyvalues"]["List"])

    def test_decrement_success(self):
        """Tests decrementing with existing accounts and valid parameters."""
        request = _request("Bob", "FooPoints", "-8", self.INCREMENT,
                           "Success case unit test.", 0)
        outcome = {
            "Key" : "FooPoints",
            "Value" : "1",
        }
        out = self.proxy.SetKeyValueEx(request)
        info = self.proxy.UserInfoEx(
            {
            "AccountName" : "Bob",
            "Flags" : USERINFO_KEYVALUES,
            }
        )
        self.assertEqual(out, self.success_value)
        self.assertIn(outcome, info["Keyvalues"]["List"])

    def test_set_failure(self):
        """Tests attempt to set with an undefined "Key" value."""
        request = _request("Bob", "", "80", self.SET,
                           "Failure case unit test.", 0)
        out = self.proxy.SetKeyValueEx(request)
        self.assertEqual(out, self.failure_value)

    def test_increment_failure(self):
        """Tests attempt to increment with an undefined "Key" value."""
        request = _request("Bob", "", "80", self.INCREMENT,
                           "Failure case unit test.",0)
        out = self.proxy.SetKeyValueEx(request)
        self.assertEqual(out, self.failure_value)

    def test_set_unf(self):
        """Tests set with an account name which doesn't exist in the server"""
        request = _request("Quiddich", "FooPoints", "80", self.SET,
                           "User-not-found case unit test.", 0)
        out = self.proxy.SetKeyValueEx(request)
        self.assertEqual(out, self.unf_value)

    def test_increment_unf(self):
        """Tests increment with an account name which doesn't exist
        in the server"""
        request = _request("Quiddich", "FooPoints", "80", self.INCREMENT,
                           "User-not-found case unit test.", 0)
        out = self.proxy.SetKeyValueEx(request)
        self.assertEqual(out, self.unf_value)

    def test_set_to_invalid_range(self):
        """Tests setting a key value to below zero"""
        request = _request("Bob", "FooPoints", "-1", self.SET,
                   "Lower out-of-bounds-value unit test.", 0)
        out = self.proxy.SetKeyValueEx(request)
        self.assertEqual(out, self.invalid_range_value)

    def test_increment_to_invalid_range(self):
        """Tests incrementing a key value to below zero"""
        request = _request("Bob", "FooPoints", "-21", self.SET,
                           "Lower out-of-bounds-value unit test.", 0)
        out = self.proxy.SetKeyValueEx(request)
        self.assertEqual(out, self.invalid_range_value)

    def test_set_beyond_upper_limit(self):
        """Tests setting a key value beyond maximum acceptable value"""
        request = _request("Bob", "FooPoints", "4294967296", self.SET,
                           "Higher out-of-bounds-value unit test.", 0)
        out = self.proxy.SetKeyValueEx(request)
        self.assertEqual(out, self.invalid_range_value)
        self.assertNotEqual(
                            self.proxy.KVGet(
                                "Bob",
                                "FooPoints",
                            ),
                            4294967296,
                        )

    def test_increment_beyond_upper_limit(self):
        """Tests incrementing a key value beyond maximum acceptable value"""
        request = _request("Bob", "FooPoints", "4294967287", self.INCREMENT,
                           "Higher out-of-bounds-value unit test.", 0)
        out = self.proxy.SetKeyValueEx(request)
        self.assertEqual(out, self.invalid_range_value)
        self.assertNotEqual(
                            self.proxy.KVGet(
                                "Bob",
                                "FooPoints",
                            ),
                            4294967296,
                        )

    def test_set_to_upper_limit(self):
        """Tests setting a key value to maximum acceptable value"""
        request = _request("Bob", "FooPoints", "4294967295",self.SET,
                           "test_success", 0)
        outcome = {
            "Key" : "FooPoints",
            "Value" : "4294967295",
        }
        out = self.proxy.SetKeyValueEx(request)
        info = self.proxy.UserInfoEx(
            {
            "AccountName" : "Bob",
            "Flags" : USERINFO_KEYVALUES,
            }
        )
        self.assertEqual(out, self.success_value)
        self.assertEqual(
                            self.proxy.KVGet(
                                "Bob",
                                "FooPoints",
                            ),
                            4294967295,
                        )

    def test_increment_to_upper_limit(self):
        """Tests incrementing a key value to maximum acceptable value"""
        request = _request("Bob", "FooPoints", "4294967286", self.INCREMENT,
                   "test_success", 0)
        outcome = {
            "Key" : "FooPoints",
            "Value" : "4294967295",
        }
        out = self.proxy.SetKeyValueEx(request)
        info = self.proxy.UserInfoEx(
            {
            "AccountName" : "Bob",
            "Flags" : USERINFO_KEYVALUES,
            }
        )
        self.assertEqual(out, self.success_value)
        self.assertEqual(
                            self.proxy.KVGet(
                                "Bob",
                                "FooPoints",
                            ),
                            4294967295,
                        )

    def test_set_nonessential_parameters(self):
        """Cycles null value through nonessential items in set request"""
        for each in self.set_requests:
            outcome = {
                "Key" : "FooPoints",
                "Value" : "80",
            }
            out = self.proxy.SetKeyValueEx(each)
            info = self.proxy.UserInfoEx(
                {
                    "AccountName" : "Bob",
                    "Flags" : USERINFO_KEYVALUES,
                }
            )
            self.assertEqual(out, self.success_value)
            self.assertIn(outcome, info["Keyvalues"]["List"])

    def test_increment_nonessential_parameters(self):
        """Cycles null value through nonessential items in increment request"""
        for each in self.increment_requests:
            value = (self.increment_requests.index(each) + 1) * 80 + 9
            outcome = {
                "Key" : "FooPoints",
                "Value" : str(value),
            }
            out = self.proxy.SetKeyValueEx(each)
            info = self.proxy.UserInfoEx(
                {
                    "AccountName" : "Bob",
                    "Flags" : USERINFO_KEYVALUES,
                }
            )
            self.assertEqual(out, self.success_value)
            self.assertIn(outcome, info["Keyvalues"]["List"])

    def test_set_nonNumeric_value(self):
        """Tries sending a set request with a non-numeric Value"""
        request = _request("Bob","BarPoints","SQUIDBOY!!!!",
                           self.SET,"Success case unit test.",0)
        outcome = {
            "Key" : "BarPoints",
            "Value" : "SQUIDBOY!!!",
        }
        out = self.proxy.SetKeyValueEx(request)
        info = self.proxy.UserInfoEx(
            {
                "AccountName" : "Bob",
                "Flags" : USERINFO_KEYVALUES,
            }
        )
        self.assertEqual(out, self.success_value)
        self.assertNotIn(outcome, info["Keyvalues"]["List"])

    def test_increment_nonNumeric_value(self):
        """Tries sending an increment request with a non-numeric Value"""
        request = _request("Bob","BarPoints","SQUIDBOY!!!!",
                           self.INCREMENT,"Success case unit test.",0)
        outcome = {
            "Key" : "FooPoints",
            "Value" : "SQUIDBOY!!!",
        }
        out = self.proxy.SetKeyValueEx(request)
        info = self.proxy.UserInfoEx(
            {
                "AccountName" : "Bob",
                "Flags" : USERINFO_KEYVALUES,
            }
        )
        self.assertEqual(out, self.success_value)
        self.assertNotIn(outcome, info["Keyvalues"]["List"])


if __name__ == '__main__':
    unittest.main()
