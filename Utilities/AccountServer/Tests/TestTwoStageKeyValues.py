from xmlrpclib import ServerProxy
import unittest
import random
import time
import string
# This file contains a class specifying key value unit tests


class TestLock(unittest.TestCase):
    """Superclass storing setup() and utility methods for TestSingleLock and
    TestPairedLock"""

    def _request(self, acct, key, value, increment, reason, request_type):
        """Helper method to initialize a request from input"""
        request = {}
        if acct:
            request["AccountName"] = acct
        if key:
            request["Key"] = key
        if request:
            request["Value"] = str(value)
        if increment:
            request["Increment"] = increment
        if key:
            request["Reason"] = reason
        if request_type:
            request["Type"] = request_type
        return request

    def random_string(self, size):
        """Helper function returns random string of size characters"""
        chars = string.digits + string.ascii_letters
        return "".join(random.choice(chars) for x in range(size))

    def setUp(self):
        """Initialize the class variables"""
        self.proxy = ServerProxy("http://localhost:8081/xmlrpc")
        self.accounts = []
        self.NULL = "(null)"
        self.UNF = {"Result" : "user_not_found"}
        self.MAX = 2147483647
        self.MIN = -2147483648
        self.UPPER_LIMIT = 2147483648
        self.LOWER_LIMIT = -2147483649
        self.VALUE_MAX = 4294967295
        self.UNKNOWN_LOCKED = {"Key" : "UndefinedPoints", "Value" : "0"}
        self.lock_password = self.NULL
        self.KEY_SET = {"Result" : "key_set"}
        self.accounts.append({
            "accountName" : "Account2",
            "uID" : 1,
            "uCreatedTime" : 1,
        })
        self.accounts.append({
            "accountName" : "Account3",
            "uID" : 2,
            "uCreatedTime" : 2,
        })
        self.accounts.append( {
            "accountName" : "Account1",
            "uID" : 3,
            "uCreatedTime" : 43,
        })
        self.products = []
        pkv_change = []
        pkv_change.append({
            "Key" : "1rstPoints",
            "Value" : "9",
            "change" : 0,
        })
        self.products.append({
            "pName" : "1rstPoints",
            "pInternalName" : "BarP",
            "bRequiresNoSubs" : 1,
            "ppKeyValueChanges" : pkv_change,
            "pKeyValueChangesString" : "BarPoints += 9",
        })
        self.dbrequest = {
            "Accounts" : self.accounts,
            "Products" : self.products,
        }
        self.db_set = self.proxy.ReplaceAccountDatabase(self.dbrequest)
        request = self._request("Account2", "FillerPoints", "9", 0, "", "")
        self.a = self.proxy.SetKeyValueEx(request)
        request = self._request("Account1", "FillerPoints", "9", 0, "", "")
        self.b = self.proxy.SetKeyValueEx(request)
        request = self._request("Account3", "2ndPoints", "9", 0, "", "")
        self.r = self.proxy.SetKeyValueEx(request)
        request = self._request("Account1", "2ndPoints", "9", 0, "", "")
        self.c = self.proxy.SetKeyValueEx(request)
        request = self._request("Account2", "2ndPoints", "9", 0, "", "")
        self.d = self.proxy.SetKeyValueEx(request)
        self.proxy.GiveProduct("Account2", "1rstPoints", "1rstPoints")
        self.proxy.GiveProduct("Account1", "1rstPoints", "1rstPoints")
        self.key_list_0 = self.proxy.GetKeyValueList("Account2")
        self.key_list_1 = self.proxy.GetKeyValueList("Account1")
        self.key_value_lists = {}
        for account in self.accounts:
            x = self.proxy.GetKeyValueList(account["accountName"])
            self.key_value_lists[account["accountName"]] = x

    def change_ex(self, acct, key, value, reason, request_type):
        """Wrapper method for self._request()"""
        r = self._request(acct, key, value, 1, reason, request_type)
        return self.proxy.SetKeyValueEx(r)

    def set_ex(self, acct, key, value, reason, request_type):
        """Wrapper method for self._request()"""
        r = self._request(acct, key, value, 0, reason, request_type)
        return self.proxy.SetKeyValueEx(r)

    def set_ex_fail(self, acct, key, value, reason, request_type):
        """set_ex and asserts key was not set"""
        return self.assertNotEqual(
            self.set_ex(acct, key, value, reason, request_type),
            self.KEY_SET)

    def set_ex_success(self, acct, key, value, reason, request_type):
        """set_ex and asserts key was set"""
        return self.assertEqual(
            self.set_ex(acct, key, value, reason, request_type),
            self.KEY_SET)

    def change_ex_success(self, acct, key, value, reason, request_type):
        """set_ex and asserts key was set"""
        return self.assertEqual(
            self.change_ex(acct, key, value, reason, request_type),
            self.KEY_SET)

    def set_not_null(self, acct, key, value):
        """KVSetOnce the inputs and asserts nonnull Result"""
        self.lock_password = self.proxy.KVSetOnce(acct, key, int(value))
        return self.assertNotEqual(self.lock_password, self.NULL)

    def change_not_null(self, acct, key, value):
        """KVChangeOnce the inputs and asserts nonnull Result"""
        self.lock_password = self.proxy.KVChangeOnce(acct, key, int(value))
        return self.assertNotEqual(self.lock_password, self.NULL)

    def set_null(self, acct, key, value):
        """KVSetOnce the inputs and asserts null Result"""
        lock_password = self.proxy.KVSetOnce(acct, key, int(value))
        return self.assertEqual(lock_password, self.NULL)

    def change_null(self, acct, key, value):
        """KVChangeOnce the inputs and asserts null Result"""
        lock_password = self.proxy.KVChangeOnce(acct, key, int(value))
        return self.assertEqual(lock_password, self.NULL)

    def lists_equal(self, acct):
        """Grabs key value list and asserts equal to the self.kv_list"""
        kv_list = self.proxy.GetKeyValueList(acct)
        return self.assertEqual(kv_list, self.key_value_lists[acct])

    def lists_unequal(self, acct):
        """Grabs key value list and asserts not equal to the self.kv_list"""
        kv_list = self.proxy.GetKeyValueList(acct)
        return self.assertNotEqual(kv_list, self.key_value_lists[acct])

    def in_list(self, acct, target):
        """Grabs key value list and asserts target is in list"""
        kv_list = self.proxy.GetKeyValueList(acct)
        return self.assertIn(target, kv_list["List"])

    def not_in_list(self, acct, target):
        """Grabs key value list and asserts target is not in list"""
        kv_list = self.proxy.GetKeyValueList(acct)
        return self.assertNotIn(target, kv_list["List"])

    def is_UNF(self, acct):
        """Tries GetKeyValueList and asserts user not found"""
        kv_list = self.proxy.GetKeyValueList(acct)
        self.assertEqual(kv_list, self.UNF)

    def set_overflow(self, acct, key, value):
        """KVSetOnce and assertRaises OverflowError"""
        return self.assertRaises(OverflowError,
            self.proxy.KVSetOnce, (acct, key, int(value)))

    def change_overflow(self, acct, key, value):
        """KVChangeOnce and assertRaises OverflowError"""
        return self.assertRaises(OverflowError,
            self.proxy.KVChangeOnce, (acct, key, int(value)))

    def not_null_lock(self, acct, key):
        """KVLock and assertNot null"""
        self.lock_password = self.proxy.KVLock(acct, key)
        return self.assertNotEqual(self.lock_password, self.NULL)

    def null_lock(self, acct, key):
        """KVLock and asserts null return value"""
        password = self.proxy.KVLock(acct, key)
        return self.assertEqual(password, self.NULL)

    def lockagain_true(self, acct, key):
        """KVLockAgain and asserts return value is 1"""
        return self.assertEqual(self.proxy.KVLockAgain(
            acct, key, self.lock_password), 1)

    def lockagain_false(self, acct, key):
        """KVLockAgain and asserts return value is 0"""
        return self.assertEqual(self.proxy.KVLockAgain(
            acct, key, self.lock_password), 0)

    def kvget_equal(self, acct, key, value):
        """Asserts equal KVGet(acct, key) and value"""
        return self.assertEqual(self.proxy.KVGet(acct, key), int(value))

    def kvrollback_success(self, acct, key):
        """Asserts KVRollback(acct, key, self.lock_password) returns 1"""
        return self.assertEqual(
            self.proxy.KVRollback(acct, key, self.lock_password), 1)

    def kvrollback_fail(self, acct, key):
        """Asserts KVRollback(acct, key, self.lock_password) returns 0"""
        return self.assertEqual(
            self.proxy.KVRollback(acct, key, self.lock_password), 0)

    def kvcommit_success(self, acct, key):
        """Asserts KVCommit(acct, key, self.lock_password) returns 1"""
        return self.assertEqual(
            self.proxy.KVCommit(acct, key, self.lock_password), 1)

    def kvcommit_fail(self, acct, key):
        """Asserts KVCommit(acct, key, self.lock_password) returns 0"""
        return self.assertEqual(
            self.proxy.KVCommit(acct, key, self.lock_password), 0)

    def confirm_locked(self, acct, key, value):
        """Confirms a lock is in effect for acct, key, at value"""
        self.null_lock(acct, key)
        self.set_ex_fail(acct, key, 90, "", "")
        self.kvget_equal(acct, key, value)

    def move_null(self, acct1, key1, acct2, key2, value):
        """Helper method runs KVMoveOnce(args) and asserts returns null"""
        pwd = self.proxy.KVMoveOnce(acct1, key1, acct2, key2, int(value))
        return self.assertEqual(pwd, self.NULL)

    def move_not_null(self, acct1, key1, acct2, key2, value):
        """Helper method runs KVMoveOnce(args) and aserts returns null"""
        self.lock_password = self.proxy.KVMoveOnce(
            acct1, key1, acct2, key2, int(value))
        return self.assertNotEqual(self.lock_password, self.NULL)

    def move_overflow(self, acct1, key1, acct2, key2, value):
        """Helper method runs KVMoveOnce(args) and aserts returns null"""
        with self.assertRaises(OverflowError):
            self.proxy.KVMoveOnce(acct1, key1, acct2, key2, int(value))

    def test_dbreplace(self):
        """Establishes that locks have no persistent effects across
        database replacements"""
        self.assertEqual(self.db_set, {"Result" : "success"})
        self.set_ex_success("Account3", "UndefinedPoints", "9", "", "")
        self.not_null_lock("Account3", "UndefinedPoints")
        self.set_ex("Account2", "OtherUndefinedPoints", "9", "", "")
        out = self.proxy.ReplaceAccountDatabase(self.dbrequest)
        self.assertEqual(self.db_set, out)
        out = self.set_ex("Account3", "AmAFishPoints", "9", "", "")
        self.assertEqual(out, self.KEY_SET)

    def test_setup(self):
        """Establishes that the setup has completed correctly"""
        self.assertEqual(self.a, self.KEY_SET)
        self.assertEqual(self.b, self.KEY_SET)
        self.assertEqual(self.c, self.KEY_SET)
        self.assertNotEqual(
            self.change_ex("Account1", "1rstPoints", "-3", "", ""),
                            {"Result" : "invalid_range"})
        self.assertNotEqual
        (self.change_ex("Account2", "1rstPoints", "-3", "", ""),
                            {"Result" : "invalid_range"})
        self.assertIn("List", self.key_list_0.keys())
        item = {"Key" : "2ndPoints", "Value" : "9"}
        for account in self.key_value_lists.keys():
            self.assertEqual(self.key_value_lists[account]["Result"],
                             "user_exists")
            self.assertIn(item, self.key_value_lists[account]["List"])


class TestSingleLock(TestLock):
    """Exhaustively tests key value lock methods which act on a single account
    These are: KVLock, KVGet, KVSetOnce, KVChangeOnce, KVRollback, KVCommit"""

    def test_lock_nonzero_key(self):
        """Tests locking nonzero key value on a real account"""
        self.not_null_lock("Account1", "1rstPoints")
        self.set_ex_fail("Account1", "1rstPoints", 9001, "", "")

    def test_lock_zero_key(self):
        """Tests locking key value set to zero on a real account"""
        self.set_ex_success("Account1", "1rstPoints", 0, "", "")
        self.not_null_lock("Account1", "1rstPoints")
        self.set_ex_fail("Account1", "1rstPoints", 9001, "", "")

    def test_lock_unset_key(self):
        """Tests locking an unset (empty) key value on a real account"""
        self.not_null_lock("Account1", "UndefinedPoints")
        self.set_ex_fail("Account1", "UndefinedPoints", 9001, "", "")

    def test_lock_unknown_acct(self):
        """Establishes lock returns null if passed an unknown acct"""
        self.is_UNF("Unknown_Account")
        self.null_lock("Unknown_Account", "1rstPoints")
        self.is_UNF("Unknown_Account")

    def test_repetitious_locking(self):
        """Tests multiple consecutive attempts to lock a given key"""
        self.not_null_lock("Account1", "1rstPoints")
        for x in range(100):
            self.null_lock("Account1", "1rstPoints")
            self.set_ex_fail("Account1", "1rstPoints", 9001, "", "")

    def test_lock_timeout(self):
        """Tests whether a lock will expire after five minutes"""
        self.not_null_lock("Account1", "1rstPoints")
        self.set_ex_fail("Account1", "1rstPoints", 90, "", "")
        self.null_lock("Account1", "1rstPoints")
        time.sleep(310)
        self.set_ex_success("Account1", "1rstPoints", 92, "", "")
        self.not_null_lock("Account1", "1rstPoints")
        self.set_ex_fail("Account1", "1rstPoints", 90, "", "")

    def test_get_unknown_acct(self):
        """Asserts KVGet returns nonexistant value if given unknonw acct"""
        self.is_UNF("Unknown_Account")
        self.kvget_equal("Unknown_Account", "1rstPoints", -1)
        self.is_UNF("Unknown_Account")

    def test_get_defined_unlocked_key(self):
        """Tests whether KVGet returns the value of a known unlocked key"""
        self.kvget_equal("Account1", "1rstPoints", 9)

    def test_get_defined_locked_key(self):
        """Tests whether KVGet returns the value of a known locked key"""
        self.not_null_lock("Account1", "1rstPoints")
        self.set_ex_fail("Account1", "1rstPoints", 90, "", "")
        self.kvget_equal("Account1", "1rstPoints", 9)

    def test_get_undefined_unlocked_key(self):
        """Tests whether KVGet returns the value of an unset unlocked key"""
        self.kvget_equal("Account1", "UndefinedPoints", -1)

    def test_get_undefined_locked_key(self):
        """Tests whether KVGet returns the value of an unset unlocked key"""
        self.not_null_lock("Account1", "UndefinedPoints")
        self.set_ex_fail("Account1", "UndefinedPoints", 90, "", "")
        self.kvget_equal("Account1", "UndefinedPoints", 0)
        self.in_list("Account1", self.UNKNOWN_LOCKED)

    def test_get_break(self):
        """Tests whether KVGet breaks a lock immediately"""
        self.not_null_lock("Account1", "1rstPoints")
        self.set_ex_fail("Account1", "1rstPoints", 90, "", "")
        self.in_list("Account1", {"Key" : "1rstPoints", "Value" : "9"})
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.null_lock("Account1", "1rstPoints")
        self.set_ex_fail("Account1", "1rstPoints", 90, "", "")
        self.in_list("Account1", {"Key" : "1rstPoints", "Value" : "9"})

    def test_repetitious_get_break(self):
        """Tests whether numerous repeated calls to KVGet breaks the lock"""
        self.not_null_lock("Account1", "1rstPoints")
        self.set_ex_fail("Account1", "1rstPoints", 9001, "", "")
        self.in_list("Account1", {"Key" : "1rstPoints", "Value" : "9"})
        for x in range(100):
            self.kvget_equal("Account1", "1rstPoints", 9)
        self.null_lock("Account1", "1rstPoints")
        self.set_ex_fail("Account1", "1rstPoints", 9001, "", "")
        self.in_list("Account1", {"Key" : "1rstPoints", "Value" : "9"})

    def test_timeout_get_break(self):
        """Tests whether KVGet breaks a timed-out lock"""
        self.not_null_lock("Account1", "UndefinedPoints")
        self.set_ex_fail("Account1", "UndefinedPoints", 90, "", "")
        self.kvget_equal("Account1", "UndefinedPoints", 0)
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        time.sleep(310)
        for x in range(100):
            self.kvget_equal("Account1", "UndefinedPoints", 0)
        self.set_ex_success("Account1", "UndefinedPoints", 90, "", "")
        self.kvget_equal("Account1", "UndefinedPoints", 90)
        self.in_list("Account1", {"Key" : "UndefinedPoints", "Value" : "90"})

    def test_set_defined_key_INin(self):
        """Tests set with defined key and in-range value"""
        self.set_not_null("Account1", "1rstPoints", 90)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "1rstPoints", 90)

    def test_set_defined_key_INmax(self):
        """Tests set with defined key and maximum value"""
        self.set_not_null("Account1", "1rstPoints", self.MAX)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "1rstPoints", self.MAX)

    def test_set_defined_key_INzero(self):
        """Tests set with defined key and zero value"""
        self.set_not_null("Account1", "1rstPoints", 0)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "1rstPoints", 0)

    def test_set_defined_key_INbelow(self):
        """Tests set with defined key and below-zero value"""
        self.set_null("Account1", "1rstPoints", -1)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.not_null_lock("Account1", "1rstPoints")

    def test_set_defined_key_INabove(self):
        """Tests set with defined key and above-range value"""
        self.set_overflow("Account1", "1rstPoints", self.UPPER_LIMIT)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.not_null_lock("Account1", "1rstPoints")

    def test_set_undefined_key_INin(self):
        """Tests set with undefined key and in-range value"""
        self.set_not_null("Account1", "UndefinedPoints", 90)
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.kvget_equal("Account1", "UndefinedPoints", 90)

    def test_set_undefined_key_INmax(self):
        """Tests set with undefined key and maximum value"""
        self.set_not_null("Account1", "UndefinedPoints", self.MAX)
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.kvget_equal("Account1", "UndefinedPoints", self.MAX)

    def test_set_undefined_key_INzero(self):
        """Tests set with undefined key and zero value"""
        self.set_not_null("Account1", "UndefinedPoints", 0)
        self.confirm_locked("Account1", "UndefinedPoints", 0)
        self.in_list("Account1", self.UNKNOWN_LOCKED)

    def test_set_undefined_key_INbelow(self):
        """Tests set with undefined key and below-range value"""
        self.set_null("Account1", "UndefinedPoints", -1)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.not_null_lock("Account1", "1rstPoints")

    def test_set_undefined_key_INabove(self):
        """Tests set with undefined key and above-range value"""
        self.set_overflow("Account1", "UndefinedPoints", self.UPPER_LIMIT)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.not_null_lock("Account1", "1rstPoints")

    def test_change_defined_key_INin_TARGin(self):
        """Change with defined key, in-range input and target values"""
        self.change_not_null("Account1", "1rstPoints", 90)
        self.confirm_locked("Account1", "1rstPoints", 99)
        self.lists_equal("Account1")

    def test_change_defined_key_INin_TARGzero(self):
        """Change with defined key, in-range input and zero target value"""
        self.change_not_null("Account1", "1rstPoints", -9)
        self.confirm_locked("Account1", "1rstPoints", 0)
        self.lists_equal("Account1")

    def test_change_defined_key_INin_TARGbelow(self):
        """Change with defined key, in-range input and below-range target"""
        self.change_null("Account1", "1rstPoints", -10)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.not_null_lock("Account1", "1rstPoints")

    def test_change_defined_key_INin_TARGmax(self):
        """Change defined key with in-range input to maximum value"""
        self.set_ex_success(
            "Account1", "1rstPoints", self.VALUE_MAX - 1, "", "")
        self.change_not_null("Account1", "1rstPoints", 1)
        self.confirm_locked("Account1", "1rstPoints", self.VALUE_MAX)
        self.in_list(
            "Account1", {
                "Value" : str(self.VALUE_MAX - 1),
                "Key" : "1rstPoints",
            })
        self.kvget_equal("Account1", "1rstPoints", self.VALUE_MAX)

    def test_change_defined_key_INin_TARGabove(self):
        """Change defined key with in-range input to above-range value"""
        self.set_ex_success("Account1", "1rstPoints", self.VALUE_MAX, "", "")
        self.change_null("Account1", "1rstPoints", 1)
        self.in_list(
            "Account1", {"Value" : str(self.VALUE_MAX), "Key" : "1rstPoints"})
        self.kvget_equal("Account1", "1rstPoints", self.VALUE_MAX)
        self.not_null_lock("Account1", "1rstPoints")

    def test_change_defined_key_INzero(self):
        """Change defined key with zero input (i.e. adds 0)"""
        self.change_not_null("Account1", "1rstPoints", 0)
        self.lists_equal("Account1")
        self.confirm_locked("Account1", "1rstPoints", 9)

    def test_change_defined_key_INmin_TARGin(self):
        """Change defined key with min input to in-range value"""
        self.set_ex_success("Account1", "1rstPoints", self.MAX + 2, "", "")
        self.change_not_null("Account1", "1rstPoints", self.MIN)
        self.confirm_locked("Account1", "1rstPoints", 1)
        self.in_list(
            "Account1", {"Value" : str(self.MAX + 2), "Key" : "1rstPoints"})
        self.kvget_equal("Account1", "1rstPoints", 1)

    def test_change_defined_key_INmin_TARGzero(self):
        """Change defined key with min input to zero value"""
        self.set_ex_success("Account1", "1rstPoints", self.MAX + 1, "", "")
        self.change_not_null("Account1", "1rstPoints", self.MIN)
        self.confirm_locked("Account1", "1rstPoints", 0)
        self.in_list(
            "Account1", {"Value" : str(self.MAX + 1), "Key" : "1rstPoints"})
        self.kvget_equal("Account1", "1rstPoints", 0)

    def test_change_defined_key_INmin_TARGbelow(self):
        """Change defined key with min input to below-range value"""
        self.set_ex_success("Account1", "1rstPoints", self.MAX, "", "")
        self.change_null("Account1", "1rstPoints", self.MIN)
        self.kvget_equal("Account1", "1rstPoints", self.MAX)
        self.in_list(
            "Account1", {"Value" : str(self.MAX), "Key" : "1rstPoints"})
        self.not_null_lock("Account1", "1rstPoints")

    def test_change_defined_key_INbelow(self):
        """Tests change with defined key and below-range input value"""
        x = self.LOWER_LIMIT
        self.change_overflow("Account1", "1rstPoints", x)
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.lists_equal("Account1")
        self.not_null_lock("Account1", "1rstPoints")

    def test_change_defined_key_INabove(self):
        """Tests change with defined key and above-range input value"""
        self.change_overflow("Account1", "1rstPoints", self.UPPER_LIMIT)
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.lists_equal("Account1")
        self.not_null_lock("Account1", "1rstPoints")

    def test_change_undefined_key_INin_TARGin(self):
        """Change undefined key with in-range input to in-range value"""
        self.change_not_null("Account1", "UndefinedPoints", 90)
        self.confirm_locked("Account1", "UndefinedPoints", 90)
        self.in_list("Account1", self.UNKNOWN_LOCKED)

    def test_change_undefined_key_INin_TARGbelow(self):
        """Change undefined key with in-range input to below-range value"""
        self.change_null("Account1", "UndefinedPoints", -10)
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.lists_equal("Account1")
        self.not_null_lock("Account1", "UndefinedPoints")

    def test_change_undefined_key_INmax(self):
        """Change undefined key with maximum value input"""
        self.change_not_null("Account1", "UndefinedPoints", self.MAX)
        self.confirm_locked("Account1", "UndefinedPoints", self.MAX)
        self.in_list(
            "Account1", self.UNKNOWN_LOCKED)

    def test_change_undefined_key_INzero(self):
        """Change undefined key with zero input"""
        self.change_not_null("Account1", "UndefinedPoints", 0)
        self.confirm_locked("Account1", "UndefinedPoints", 0)
        self.in_list("Account1", self.UNKNOWN_LOCKED)

    def test_change_undefined_key_INmin(self):
        """Change undefined key with lowest accepted input"""
        self.change_null("Account1", "UndefinedPoints", self.MIN)
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.lists_equal("Account1")
        self.not_null_lock("Account1", "UndefinedPoints")

    def test_change_undefined_key_INbelow(self):
        """Change undefined key with a below-range input"""
        self.change_overflow("Account1", "UndefinedPoints", self.LOWER_LIMIT)
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.lists_equal("Account1")
        self.not_null_lock("Account1", "UndefinedPoints")

    def test_change_undefined_key_INabove(self):
        """Change undefined key with an above-range input"""
        self.change_overflow("Account1", "UndefinedPoints", self.UPPER_LIMIT)
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.lists_equal("Account1")
        self.not_null_lock("Account1", "UndefinedPoints")

    def test_rollback_known_acct_locked_defined_key(self):
        """Rollback a locked defined key of known account"""
        self.not_null_lock("Account1", "1rstPoints")
        self.confirm_locked("Account1", "1rstPoints", 9)
        self.lists_equal("Account1")
        self.kvrollback_success("Account1", "1rstPoints")
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.lists_equal("Account1")
        self.not_null_lock("Account1", "1rstPoints")

    def test_rollback_known_acct_locked_undefined_key(self):
        """Rollback a locked undefined key of known account"""
        self.not_null_lock("Account1", "UndefinedPoints")
        self.confirm_locked("Account1", "UndefinedPoints", 0)
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.kvrollback_success("Account1", "UndefinedPoints")
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.lists_equal("Account1")
        self.not_null_lock("Account1", "1rstPoints")

    def test_rollback_known_acct_unlocked_defined_key(self):
        """Rollback known account's unlocked defined key of fails without
        breaking anything"""
        self.kvrollback_fail("Account1", "1rstPoints")
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.lists_equal("Account1")
        self.set_ex_success("Account1", "1rstPoints", 90, "", "")
        self.not_null_lock("Account1", "1rstPoints")

    def test_rollback_known_acct_unlocked_undefined_key(self):
        """Rollback known account's unlocked defined key of fails without
        breaking anything"""
        self.kvrollback_fail("Account1", "UndefinedPoints")
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.lists_equal("Account1")
        self.set_ex_success("Account1", "UndefinedPoints", 90, "", "")
        self.not_null_lock("Account1", "UndefinedPoints")

    def test_rollback_unknown_acct_unlocked_undefined_key(self):
        """Rollback unknown account fails without breaking anything"""
        self.is_UNF("Unknown_Account")
        self.kvrollback_fail("Unknown_Account", "1rstPoints")
        self.is_UNF("Unknown_Account")
        self.kvget_equal("Unknown_Account", "1rstPoints", -1)

    def test_rollback_numerous_password_fails_success(self):
        """Locks, attempts many rollbacks with bad passwords, then rollback
        with correct password"""
        self.not_null_lock("Account1", "1rstPoints")
        bad_passwords = []
        for x in range(10000):
            a_password = self.random_string(random.randint(1, 60))
            while a_password is self.lock_password:
                a_password = self.random_string(random.randint(1, 60))
            bad_passwords.append(a_password)
        for bad_password in bad_passwords:
            self.assertEqual(
                self.proxy.KVRollback("Account1", "1rstPoints", bad_password),
                0)
        self.kvrollback_success("Account1", "1rstPoints")

    def test_commit_unknown_acct(self):
        """Commit an unknown account's (undefined unlocked) key"""
        self.is_UNF("Unknown_Account")
        self.kvcommit_fail("Unknown_Account", "1rstPoints")
        self.is_UNF("Unknown_Account")

    def test_commit_known_acct_unlocked_defined_key(self):
        """Commit an unlocked defined key on a known account"""
        self.kvcommit_fail("Account1", "1rstPoints")
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.lists_equal("Account1")
        self.not_null_lock("Account1", "1rstPoints")

    def test_commit_known_acct_locked_defined_key(self):
        """Lock a defined key on a known account, and commit"""
        self.not_null_lock("Account1", "1rstPoints")
        self.confirm_locked("Account1", "1rstPoints", 9)
        self.lists_equal("Account1")
        self.kvcommit_success("Account1", "1rstPoints")
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.lists_equal("Account1")
        self.set_ex_success("Account1", "1rstPoints", 90, "", "")
        self.not_null_lock("Account1", "1rstPoints")

    def test_commit_known_acct_locked_undefined_key(self):
        """Lock and commit defined key on a known account; should fail"""
        self.not_null_lock("Account1", "UndefinedPoints")
        self.confirm_locked("Account1", "UndefinedPoints", 0)
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.kvcommit_success("Account1", "UndefinedPoints")
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "UndefinedPoints", -1)

    def test_commit_numerous_password_fails_success(self):
        """Locks, attempts many rollbacks with bad passwords, then rollback
        with correct password"""
        self.not_null_lock("Account1", "1rstPoints")
        bad_passwords = []
        for x in range(10000):
            a_password = self.random_string(random.randint(1, 60))
            while a_password is self.lock_password:
                a_password = self.random_string(random.randint(1, 60))
            bad_passwords.append(a_password)
        for bad_password in bad_passwords:
            self.assertEqual(
                self.proxy.KVCommit("Account1", "1rstPoints", bad_password), 0)
        self.kvcommit_success("Account1", "1rstPoints")

    def test_success_rollback_rollback(self):
        """Lock defined key, rollback|commit randomly, rollback twice;
        both rollbacks should fail; repeats 32 times for coverage"""
        for a in range(32):
            self.not_null_lock("Account1", "1rstPoints")
            self.confirm_locked("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")
            x = random.randint(1, 2)
            if x is 1:
                self.kvrollback_success("Account1", "1rstPoints")
            else:
                self.kvcommit_success("Account1", "1rstPoints")
            self.kvget_equal("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")
            self.kvrollback_fail("Account1", "1rstPoints")
            self.kvget_equal("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")
            self.kvrollback_fail("Account1", "1rstPoints")
            self.kvget_equal("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")

    def test_success_rollback_commit(self):
        """Lock defined key, rollback|commit randomly, rollback twice; both
        rollback and commit should fail; repeats 32 times for coverage"""
        for a in range(32):
            self.not_null_lock("Account1", "1rstPoints")
            self.confirm_locked("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")
            x = random.randint(1, 2)
            if x is 1:
                self.kvrollback_success("Account1", "1rstPoints")
            else:
                self.kvcommit_success("Account1", "1rstPoints")
            self.kvget_equal("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")
            self.kvrollback_fail("Account1", "1rstPoints")
            self.kvget_equal("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")
            self.kvcommit_fail("Account1", "1rstPoints")
            self.kvget_equal("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")

    def test_success_commit_rollback(self):
        """Lock defined key, rollback|commit randomly, rollback twice; both
        commit and rollback should fail; repeats 32 times for coverage"""
        for a in range(32):
            self.not_null_lock("Account1", "1rstPoints")
            self.confirm_locked("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")
            x = random.randint(1, 2)
            if x is 1:
                self.kvrollback_success("Account1", "1rstPoints")
            else:
                self.kvcommit_success("Account1", "1rstPoints")
            self.kvget_equal("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")
            self.kvcommit_fail("Account1", "1rstPoints")
            self.kvget_equal("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")
            self.kvrollback_fail("Account1", "1rstPoints")
            self.kvget_equal("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")

    def test_success_commit_commit(self):
        """Lock defined key, rollback|commit randomly, commit twice;
        both commits should fail; repeats 32 times for coverage"""
        for a in range(32):
            self.not_null_lock("Account1", "1rstPoints")
            self.confirm_locked("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")
            x = random.randint(1, 2)
            if x is 1:
                self.kvrollback_success("Account1", "1rstPoints")
            else:
                self.kvcommit_success("Account1", "1rstPoints")
            self.kvget_equal("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")
            self.kvcommit_fail("Account1", "1rstPoints")
            self.kvget_equal("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")
            self.kvcommit_fail("Account1", "1rstPoints")
            self.kvget_equal("Account1", "1rstPoints", 9)
            self.lists_equal("Account1")

    def test_lazy_timeout(self):
        """Tests that lock timeout doesn't change until something is
        done to it (waits ten min presently)"""
        self.set_not_null("Account1", "1rstPoints", 99)
        self.confirm_locked("Account1", "1rstPoints", 99)
        self.lists_equal("Account1")
        time.sleep(600)
        self.kvget_equal("Account1", "1rstPoints", 99)
        self.lists_equal("Account1")
        self.set_ex_success("Account1", "1rstPoints", 6, "", "")
        self.in_list("Account1", {"Value" : "6", "Key" : "1rstPoints"})


class TestPairedLock(TestLock):
    """Exhaustively tests key value lock methods which acts on two accounts
    These are: KVMoveOnce"""

    def test_move_acct_unknown_keys_same(self):
        """Tries moving the same key to the same key on unknown account"""
        self.is_UNF("Unknown_Account")
        self.move_null(
            "Unknown_Account", "1rstPoints", "Unknown_Account",
            "1rstPoints", 1)
        self.is_UNF("Unknown_Account")

    def test_move_acct_known_keys_same_defined_INin(self):
        """Moves in-range amount from known acct's defined key to same acct's
        same key"""
        self.move_null("Account1", "1rstPoints", "Account1", "1rstPoints", 1)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "1rstPoints", 9)

    def test_move_acct_known_keys_same_defined_INabove(self):
        """Moves above-range amount from known acct's defined key to same
        acct's same key"""
        self.move_overflow(
            "Account1", "1rstPoints", "Account1", "1rstPoints",
            self.UPPER_LIMIT)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "1rstPoints", 9)

    def test_move_acct_known_keys_same_defined_INzero(self):
        """Moves zero amount from known acct's defined key to same
        acct's same key"""
        self.move_null("Account1", "1rstPoints", "Account1", "1rstPoints", 0)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "1rstPoints", 9)

    def test_move_acct_known_keys_same_defined_INbelow(self):
        """Moves below-range amount from known acct's defined key to same
        acct's same key"""
        self.move_null("Account1", "1rstPoints", "Account1", "1rstPoints", -1)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "1rstPoints", 9)

    def test_move_acct_known_keys_same_undefined(self):
        """Moves from known account's undefined key to same known's same key"""
        self.move_null(
            "Account1", "UndefinedPoints", "Account1", "UndefinedPoints", 1)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "UndefinedPoints", -1)

    def test_move_acct_known_keys_undefined_defined(self):
        """Tries moving from undefined key to defined key, same acct"""
        self.move_null(
            "Account1", "UndefinedPoints", "Account1", "1rstPoints", 1)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.kvget_equal("Account1", "UndefinedPoints", -1)

    def test_move_acct_known_keys_defined_defined_INin_TARGin_SRCin(self):
        """Move in-range value from defined key to other defined key, same
        acct; target and source should be in-range after commit"""
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "2ndPoints", 1)
        self.confirm_locked("Account1", "1rstPoints", 8)
        self.confirm_locked("Account1", "2ndPoints", 10)
        self.lists_equal("Account1")

    def test_move_acct_known_keys_defined_defined_INin_TARGin_SRCzero(self):
        """Move in-range value from defined key to other defined key, same
        acct; target in-range, source zero after commit"""
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "2ndPoints", 9)
        self.confirm_locked("Account1", "1rstPoints", 0)
        self.confirm_locked("Account1", "2ndPoints", 18)
        self.lists_equal("Account1")

    def test_move_acct_known_keys_defined_defined_INin_TARGin_SRCbelow(self):
        """Move in-range value from defined key to other defined key, same
        acct; target in-range, source below-range upon commit"""
        self.move_null("Account1", "1rstPoints", "Account1", "2ndPoints", 10)
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.kvget_equal("Account1", "2ndPoints", 9)
        self.lists_equal("Account1")

    def test_move_acct_known_keys_defined_defined_INin_TARGmax_SRCin(self):
        """Move in-range value from defined key to other defined key, same
        acct; target max value, source in-range after commit"""
        self.set_ex_success(
            "Account1", "2ndPoints", self.VALUE_MAX - 1, "", "")
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "2ndPoints", 1)
        self.confirm_locked("Account1", "1rstPoints", 8)
        self.confirm_locked("Account1", "2ndPoints", self.VALUE_MAX)
        self.in_list("Account1", {"Value" : "9", "Key" : "1rstPoints"})
        self.in_list("Account1", {
            "Value" : str(self.VALUE_MAX - 1),
            "Key" : "2ndPoints",
        })

    def test_move_acct_known_keys_defined_defined_INin_TARGmax_SRCzero(self):
        """Move in-range value from defined key to other defined key, same
        acct; target max value, source zero after commit"""
        self.set_ex_success(
            "Account1", "2ndPoints", self.VALUE_MAX - 9, "", "")
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "2ndPoints", 9)
        self.confirm_locked("Account1", "1rstPoints", 0)
        self.confirm_locked("Account1", "2ndPoints", self.VALUE_MAX)
        self.in_list("Account1", {"Value" : "9", "Key" : "1rstPoints"})
        self.in_list("Account1", {
            "Value" : str(self.VALUE_MAX - 9),
            "Key" : "2ndPoints",
        })

    def test_move_acct_known_keys_defined_defined_INin_TARGmax_SRCbelow(self):
        """Move in-range value from defined key to other defined key, same
        acct; target max value, source below-range upon commit"""
        self.set_ex_success(
            "Account1", "2ndPoints", self.VALUE_MAX - 10, "", "")
        self.move_null("Account1", "1rstPoints", "Account1", "2ndPoints", 10)
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.kvget_equal("Account1", "2ndPoints", self.VALUE_MAX - 10)
        self.in_list("Account1", {"Value" : "9", "Key" : "1rstPoints"})
        self.in_list("Account1", {
            "Value" : str(self.VALUE_MAX - 10),
            "Key" : "2ndPoints",
        })

    def test_move_acct_known_keys_defined_defined_INin_TARGabove_SRCin(self):
        """Move in-range value from defined key to other defined key, same
        acct; target above, source in-range upon commit"""
        self.set_ex_success("Account1", "2ndPoints", self.VALUE_MAX, "", "")
        self.move_null("Account1", "1rstPoints", "Account1", "2ndPoints", 1)
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.kvget_equal("Account1", "2ndPoints", self.VALUE_MAX)
        self.in_list("Account1", {"Value" : "9", "Key" : "1rstPoints"})
        self.in_list("Account1", {
            "Value" : str(self.VALUE_MAX),
            "Key" : "2ndPoints",
        })

    def test_move_acct_known_keys_defined_defined_INmax_TARGin_SRCin(self):
        """Move max value from defined key to other defined key, same
        acct; target and source should be in-range after commit"""
        self.set_ex_success("Account1", "1rstPoints", self.VALUE_MAX, "", "")
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "2ndPoints", self.MAX)
        self.confirm_locked("Account1", "1rstPoints",
                            self.VALUE_MAX - self.MAX)
        self.confirm_locked("Account1", "2ndPoints", self.MAX + 9)
        self.in_list("Account1", {"Value" : "9", "Key" : "2ndPoints"})
        self.in_list("Account1", {
            "Value" : str(self.VALUE_MAX),
            "Key" : "1rstPoints",
        })

    def test_move_acct_known_keys_defined_defined_INmax_TARGin_SRCzero(self):
        """Move max value from defined key to other defined key, same
        acct; target in-range, source zero after commit"""
        self.set_ex_success("Account1", "1rstPoints", self.MAX, "", "")
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "2ndPoints", self.MAX)
        self.confirm_locked("Account1", "1rstPoints", 0)
        self.confirm_locked("Account1", "2ndPoints", self.MAX + 9)
        self.in_list("Account1", {"Value" : "9", "Key" : "2ndPoints"})
        self.in_list("Account1", {
            "Value" : str(self.MAX),
            "Key" : "1rstPoints",
        })

    def test_move_acct_known_keys_defined_defined_INmax_TARGin_SRCbelow(self):
        """Move max value from defined key to other defined key, same
        acct; target in-range, source zero after commit"""
        self.set_ex_success("Account1", "1rstPoints", self.MAX - 1, "", "")
        self.move_null(
            "Account1", "1rstPoints", "Account1", "2ndPoints", self.MAX)
        self.kvget_equal("Account1", "1rstPoints", self.MAX - 1)
        self.kvget_equal("Account1", "2ndPoints", 9)
        self.in_list("Account1", {"Value" : "9", "Key" : "2ndPoints"})
        self.in_list("Account1", {
            "Value" : str(self.MAX - 1),
            "Key" : "1rstPoints",
        })

    def test_move_acct_known_keys_defined_defined_INabove_TARGin_SRCin(self):
        """Move above-range value from defined key to other defined key, same
        acct; target in-range, source in-range if move completed"""
        self.set_ex_success("Account1", "1rstPoints", self.MAX + 2, "", "")
        self.move_overflow(
            "Account1", "1rstPoints", "Account1", "2ndPoints",
            self.UPPER_LIMIT)
        self.kvget_equal("Account1", "1rstPoints", self.MAX + 2)
        self.kvget_equal("Account1", "2ndPoints", 9)
        self.in_list("Account1", {"Value" : "9", "Key" : "2ndPoints"})
        self.in_list("Account1", {
            "Value" : str(self.MAX + 2),
            "Key" : "1rstPoints",
        })

    def test_move_acct_known_keys_defined_defined_INbelow(self):
        """Move below-range value from defined key to other defined key, same
        acct"""
        self.move_null("Account1", "1rstPoints", "Account1", "2ndPoints", -1)
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.kvget_equal("Account1", "2ndPoints", 9)
        self.lists_equal("Account1")

    def test_move_acct_known_keys_defined_undefined(self):
        """Tries moving from defined key to undefined key, same acct"""
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "UndefinedPoints", 1)
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.confirm_locked("Account1", "1rstPoints", 8)
        self.confirm_locked("Account1", "UndefinedPoints", 1)

    def test_move_accts_unknown(self):
        """Tries moving between two unknown accounts"""
        self.is_UNF("Unknown_Account")
        self.is_UNF("Hyougo_Prefecture")
        self.move_null(
            "Unknown_Account", "1rstPoints", "Hyougo_Prefecture",
            "RoundThings", 5)
        self.is_UNF("Unknown_Account")
        self.is_UNF("Hyougo_Prefecture")

    def test_move_accts_known_unknown(self):
        """Tries moving from a known acct to an unknown acct"""
        self.is_UNF("Unknown_Account")
        self.move_null(
            "Account1", "1rstPoints", "Unknown_Account", "1rstPoints", 1)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.is_UNF("Unknown_Account")

    def test_move_accts_unknown_known(self):
        """Tries moving from an unknown acct to a known acct"""
        self.is_UNF("Unknown_Account")
        self.move_null(
            "Unknown_Account", "1rstPoints", "Account1", "1rstPoints", 1)
        self.lists_equal("Account1")
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.is_UNF("Unknown_Account")

    def test_move_accts_known_keys_same_defined_INin_TARGin_SRCin(self):
        """Moves from known account's defined key to other known's same key;
        source key value > 0 if committed"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "1rstPoints", 10)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_move_accts_known_keys_same_defined_INin_TARGin_SRCzero(self):
        """Moves from known account's defined key to other known's same key
        source key value = 0 if committed"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", 9)
        self.confirm_locked("Account2", "1rstPoints", 0)
        self.confirm_locked("Account1", "1rstPoints", 18)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_move_accts_known_keys_same_defined_INin_TARGin_SRCbelow(self):
        """Moves from known account's defined key to other known's same key;
        source key value < 0 if committed"""
        self.move_null("Account2", "1rstPoints", "Account1", "1rstPoints", 10)
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_move_accts_known_keys_same_defined_INin_TARGmax_SRCin(self):
        """Moves from known account's defined key to other known's same key;
        source key value < 0 if committed"""
        self.set_ex_success(
            "Account1", "1rstPoints", self.VALUE_MAX - 1, "", "")
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "1rstPoints", self.VALUE_MAX)
        self.lists_equal("Account2")
        self.in_list(
            "Account1", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX - 1),
            })

    def test_move_accts_known_keys_same_defined_INin_TARGmax_SRCzero(self):
        """Moves from known account's defined key to other known's same key;
        source key value = 0 if committed"""
        self.set_ex_success(
            "Account1", "1rstPoints", self.VALUE_MAX - 9, "", "")
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", 9)
        self.confirm_locked("Account2", "1rstPoints", 0)
        self.confirm_locked("Account1", "1rstPoints", self.VALUE_MAX)
        self.lists_equal("Account2")
        self.in_list(
            "Account1", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX - 9)
            })

    def test_move_accts_known_keys_same_defined_INin_TARGmax_SRCbelow(self):
        """Moves from known account's defined key to other known's same key;
        source key value > 0 if committed"""
        self.set_ex_success(
            "Account1", "1rstPoints", self.VALUE_MAX - 10, "", "")
        self.move_null("Account2", "1rstPoints", "Account1", "1rstPoints", 10)
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.kvget_equal("Account1", "1rstPoints", self.VALUE_MAX - 10)
        self.lists_equal("Account2")
        self.in_list(
            "Account1", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX - 10),
            })

    def test_move_accts_known_keys_same_defined_INin_TARGabove_SRCin(self):
        """Moves from known account's defined key to other known's same key;
        source key value > 0 if committed"""
        self.set_ex_success("Account1", "1rstPoints", self.VALUE_MAX, "", "")
        self.move_null("Account2", "1rstPoints", "Account1", "1rstPoints", 1)
        self.lists_equal("Account2")
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.in_list(
            "Account1", {"Key" : "1rstPoints", "Value" : str(self.VALUE_MAX)})
        self.kvget_equal("Account1", "1rstPoints", self.VALUE_MAX)

    def test_move_accts_known_keys_same_defined_INzero(self):
        """Moves from known account's defined key to other known's same key"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", 0)
        self.confirm_locked("Account2", "1rstPoints", 9)
        self.confirm_locked("Account1", "1rstPoints", 9)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_move_accts_known_keys_same_defined_INmax_TARGin_SRCin(self):
        """Moves from known account's defined key to other known's same key;
        source key value > 0 if committed"""
        self.change_ex_success("Account2", "1rstPoints", self.MAX, "", "")
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", self.MAX)
        self.confirm_locked("Account2", "1rstPoints", 9)
        self.confirm_locked("Account1", "1rstPoints", self.MAX + 9)
        self.lists_equal("Account1")
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.MAX + 9)})

    def test_move_accts_known_keys_same_defined_INmax_TARGin_SRCzero(self):
        """Moves from known account's defined key to other known's same key;
        source key value = 0 if committed"""
        self.change_ex_success("Account2", "1rstPoints", self.MAX - 9, "", "")
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", self.MAX)
        self.confirm_locked("Account2", "1rstPoints", 0)
        self.confirm_locked("Account1", "1rstPoints", self.MAX + 9)
        self.lists_equal("Account1")
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.MAX)})

    def test_move_accts_known_keys_same_defined_INmax_TARGin_SRCbelow(self):
        """Moves from known account's defined key to other known's same key;
        source key value < 0 if committed"""
        self.change_ex_success("Account2", "1rstPoints", self.MAX - 10, "", "")
        self.move_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", self.MAX)
        self.kvget_equal("Account2", "1rstPoints", self.MAX - 1)
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.lists_equal("Account1")
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.MAX - 1)})

    def test_move_accts_known_keys_same_defined_INmax_TARGmax_SRCin(self):
        """Moves from known account's defined key to other known's same key;
        source key value > 0 if committed"""
        self.change_ex_success("Account2", "1rstPoints", self.MAX, "", "")
        self.set_ex_success(
            "Account1", "1rstPoints", self.VALUE_MAX - self.MAX, "", "")
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", self.MAX)
        self.confirm_locked("Account2", "1rstPoints", 9)
        self.confirm_locked("Account1", "1rstPoints", self.VALUE_MAX)
        self.in_list(
            "Account1", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX - self.MAX)
            })
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.MAX + 9)})

    def test_move_accts_known_keys_same_defined_INmax_TARGmax_SRCzero(self):
        """Moves from known account's defined key to other known's same key;
        source key value = 0 if committed"""
        self.change_ex_success("Account2", "1rstPoints", self.MAX - 9, "", "")
        self.set_ex_success(
            "Account1", "1rstPoints", self.VALUE_MAX - self.MAX, "", "")
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", self.MAX)
        self.confirm_locked("Account2", "1rstPoints", 0)
        self.confirm_locked("Account1", "1rstPoints", self.VALUE_MAX)
        self.in_list(
            "Account1", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX - self.MAX)
            })
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.MAX)})

    def test_move_accts_known_keys_same_defined_INmax_TARGmax_SRCbelow(self):
        """Moves from known account's defined key to other known's same key;
        source key value < 0 if committed"""
        self.change_ex_success("Account2", "1rstPoints", self.MAX - 10, "", "")
        self.set_ex_success(
            "Account1", "1rstPoints", self.VALUE_MAX - self.MAX, "", "")
        self.move_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", self.MAX)
        self.kvget_equal("Account2", "1rstPoints", self.MAX - 1)
        self.kvget_equal("Account1", "1rstPoints", self.VALUE_MAX - self.MAX)
        self.in_list(
            "Account1", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX - self.MAX)
            })
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.MAX - 1)})

    def test_move_accts_known_keys_same_defined_INmax_TARGabove_SRCin(self):
        """Moves from known account's defined key to other known's same key"""
        self.change_ex_success("Account2", "1rstPoints", self.MAX, "", "")
        self.set_ex_success(
            "Account1", "1rstPoints", self.VALUE_MAX - self.MAX + 1, "", "")
        self.move_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", self.MAX)
        self.kvget_equal("Account2", "1rstPoints", self.MAX + 9)
        self.kvget_equal(
            "Account1", "1rstPoints", self.VALUE_MAX - self.MAX + 1)
        self.in_list(
            "Account1", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX - self.MAX + 1)
            })
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.MAX + 9)})

    def test_move_accts_known_keys_same_defined_INabove(self):
        """Moves above-range value from known account's defined key to other
        known's same key; source key value > 0 if committed"""
        self.set_ex_success("Account2", "1rstPoints", self.VALUE_MAX, "", "")
        self.set_ex_success(
            "Account1", "1rstPoints", self.VALUE_MAX - self.MAX + 1, "", "")
        self.move_overflow(
            "Account2", "1rstPoints", "Account1", "1rstPoints",
            self.UPPER_LIMIT)
        self.kvget_equal("Account2", "1rstPoints", self.VALUE_MAX)
        self.kvget_equal(
            "Account1", "1rstPoints", self.VALUE_MAX - self.MAX + 1)
        self.in_list(
            "Account1", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX - self.MAX + 1)
            })
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.VALUE_MAX)})

    def test_move_accts_known_keys_same_defined_INbelow(self):
        """Moves below-range value from known account's defined key to other
        known's same key; source key value > 0 if committed"""
        self.set_ex_success("Account2", "1rstPoints", self.VALUE_MAX, "", "")
        self.set_ex_success(
            "Account1", "1rstPoints", self.VALUE_MAX - self.MAX + 1, "", "")
        self.move_overflow(
            "Account2", "1rstPoints", "Account1", "1rstPoints",
            self.LOWER_LIMIT)
        self.kvget_equal("Account2", "1rstPoints", self.VALUE_MAX)
        self.kvget_equal(
            "Account1", "1rstPoints", self.VALUE_MAX - self.MAX + 1)
        self.in_list(
            "Account1", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX - self.MAX + 1)
            })
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.VALUE_MAX)})

    def test_move_accts_known_keys_defined_defined_INin_TARGin_SRCin(self):
        """Moves in-range value from known account's defined key to other
        known's other key; target in-range, source in-range if committed"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "2ndPoints", 10)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_move_accts_known_keys_defined_defined_INin_TARGin_SRCzero(self):
        """Moves in-range value from known account's defined key to other
        known's other key; target in-range, source zero if committed"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", 9)
        self.confirm_locked("Account2", "1rstPoints", 0)
        self.confirm_locked("Account1", "2ndPoints", 18)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_move_accts_known_keys_defined_defined_INin_TARGin_SRCbelow(self):
        """Moves in-range value from known account's defined key to other
        known's other key; target in-range, source below if committed"""
        self.move_null("Account2", "1rstPoints", "Account1", "2ndPoints", 10)
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.kvget_equal("Account1", "2ndPoints", 9)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_move_accts_known_keys_defined_defined_INin_TARGmax_SRCin(self):
        """Moves in-range value from known account's defined key to other
        known's other key; target max value, source in-range if committed"""
        self.set_ex_success(
            "Account1", "2ndPoints", self.VALUE_MAX - 1, "", "")
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "2ndPoints", self.VALUE_MAX)
        self.lists_equal("Account2")
        self.in_list(
            "Account1", {
                "Key" : "2ndPoints",
                "Value" : str(self.VALUE_MAX - 1),
            })

    def test_move_accts_known_keys_defined_defined_INin_TARGmax_SRCzero(self):
        """Moves in-range value from known account's defined key to other
        known's other key; target max value, source zero if committed"""
        self.set_ex_success(
            "Account1", "2ndPoints", self.VALUE_MAX - 9, "", "")
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", 9)
        self.confirm_locked("Account2", "1rstPoints", 0)
        self.confirm_locked("Account1", "2ndPoints", self.VALUE_MAX)
        self.lists_equal("Account2")
        self.in_list(
            "Account1", {
                "Key" : "2ndPoints",
                "Value" : str(self.VALUE_MAX - 9),
            })

    def test_move_accts_known_keys_defined_defined_INin_TARGmax_SRCbel(self):
        """Moves in-range value from known account's defined key to other
        known's other key; target max value, source below if committed"""
        self.set_ex_success(
            "Account1", "2ndPoints", self.VALUE_MAX - 10, "", "")
        self.move_null("Account2", "1rstPoints", "Account1", "2ndPoints", 10)
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.kvget_equal("Account1", "2ndPoints", self.VALUE_MAX - 10)
        self.lists_equal("Account2")
        self.in_list(
            "Account1", {
                "Key" : "2ndPoints",
                "Value" : str(self.VALUE_MAX - 10),
            })

    def test_move_accts_known_keys_defined_defined_INin_TARGabove(self):
        """Moves in-range value from known account's defined key to other
        known's other key; target above-range, source in-range if committed"""
        self.set_ex_success("Account1", "2ndPoints", self.VALUE_MAX, "", "")
        self.move_null("Account2", "1rstPoints", "Account1", "2ndPoints", 1)
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.kvget_equal("Account1", "2ndPoints", self.VALUE_MAX)
        self.lists_equal("Account2")
        self.in_list(
            "Account1", {"Key" : "2ndPoints", "Value" : str(self.VALUE_MAX)})

    def test_move_accts_known_keys_defined_defined_INzero(self):
        """Moves in-range value from known account's defined key to other
        known's other key"""
        self.move_not_null("Account2", "1rstPoints", "Account1", "2ndPoints", 0)
        self.confirm_locked("Account2", "1rstPoints", 9)
        self.confirm_locked("Account1", "2ndPoints", 9)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_move_accts_known_keys_defined_defined_INbelow(self):
        """Moves below-range value from known account's defined key to other
        known's other key"""
        self.move_null("Account2", "1rstPoints", "Account1", "2ndPoints", -1)
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.kvget_equal("Account1", "2ndPoints", 9)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_move_accts_known_keys_defined_defined_INmax_TARGin_SRCin(self):
        """Moves max value from known account's defined key to other
        known's other key; target in-range, source in-range if committed"""
        self.set_ex_success("Account2", "1rstPoints", self.VALUE_MAX, "", "")
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", self.MAX)
        self.confirm_locked("Account2", "1rstPoints",
                            self.VALUE_MAX - self.MAX)
        self.confirm_locked("Account1", "2ndPoints", self.MAX + 9)
        self.lists_equal("Account1")
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.VALUE_MAX)})

    def test_move_accts_known_keys_defined_defined_INmax_TARGin_SRCzero(self):
        """Moves max value from known account's defined key to other
        known's other key; target in-range, source zero if committed"""
        self.set_ex_success("Account2", "1rstPoints", self.MAX, "", "")
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", self.MAX)
        self.confirm_locked("Account2", "1rstPoints", 0)
        self.confirm_locked("Account1", "2ndPoints", self.MAX + 9)
        self.lists_equal("Account1")
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.MAX)})

    def test_move_accts_known_keys_defined_defined_INmax_TARGin_SRCbel(self):
        """Moves max value from known account's defined key to other
        known's other key; target in-range, source below-range if committed"""
        self.set_ex_success("Account2", "1rstPoints", self.MAX - 10, "", "")
        self.move_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", self.MAX)
        self.kvget_equal("Account1", "2ndPoints", 9)
        self.kvget_equal("Account2", "1rstPoints", self.MAX - 10)
        self.lists_equal("Account1")
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.MAX - 10)})

    def test_move_accts_known_keys_defined_defined_INmax_TARGmax_SRCin(self):
        """Moves max value from known account's defined key to other
        known's other key; target max, source in-range if committed"""
        self.set_ex_success("Account2", "1rstPoints", self.VALUE_MAX, "", "")
        self.set_ex_success(
            "Account1", "2ndPoints", self.VALUE_MAX - self.MAX, "", "")
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", self.MAX)
        self.confirm_locked(
            "Account2", "1rstPoints", self.VALUE_MAX - self.MAX)
        self.confirm_locked("Account1", "2ndPoints", self.VALUE_MAX)
        self.in_list(
            "Account2", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX),
            })
        self.in_list(
            "Account1", {
                "Key" : "2ndPoints",
                "Value" : str(self.VALUE_MAX - self.MAX),
            })

    def test_move_accts_known_keys_defined_defined_INmax_TARGmax_SRC0(self):
        """Moves max value from known account's defined key to other
        known's other key; target max, source zero if committed"""
        self.set_ex_success("Account2", "1rstPoints", self.MAX, "", "")
        self.set_ex_success(
            "Account1", "2ndPoints", self.VALUE_MAX - self.MAX, "", "")
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", self.MAX)
        self.confirm_locked("Account2", "1rstPoints", 0)
        self.confirm_locked("Account1", "2ndPoints", self.VALUE_MAX)
        self.in_list(
            "Account2", {
                "Key" : "1rstPoints",
                "Value" : str(self.MAX),
            })
        self.in_list(
            "Account1", {
                "Key" : "2ndPoints",
                "Value" : str(self.VALUE_MAX - self.MAX),
            })

    def test_move_accts_known_keys_defined_defined_INmax_TARGmax_SRCbel(self):
        """Moves max value from known account's defined key to other
        known's other key; target max, source below-range if committed"""
        self.set_ex_success("Account2", "1rstPoints", self.MAX - 1, "", "")
        self.set_ex_success(
            "Account1", "2ndPoints", self.VALUE_MAX - self.MAX, "", "")
        self.move_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", self.MAX)
        self.kvget_equal("Account2", "1rstPoints", self.MAX - 1)
        self.kvget_equal("Account1", "2ndPoints", self.VALUE_MAX - self.MAX)
        self.in_list(
            "Account2", {
                "Key" : "1rstPoints",
                "Value" : str(self.MAX - 1),
            })
        self.in_list(
            "Account1", {
                "Key" : "2ndPoints",
                "Value" : str(self.VALUE_MAX - self.MAX),
            })

    def test_move_accts_known_keys_defined_defined_INmax_TARGabove(self):
        """Moves max value from known account's defined key to other
        known's other key; target above-range, source in-range if committed"""
        self.set_ex_success("Account2", "1rstPoints", self.VALUE_MAX, "", "")
        self.set_ex_success(
            "Account1", "2ndPoints", self.VALUE_MAX - self.MAX + 1, "", "")
        self.move_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", self.MAX)
        self.kvget_equal("Account2", "1rstPoints", self.VALUE_MAX)
        self.kvget_equal("Account1", "2ndPoints",
                         self.VALUE_MAX - self.MAX + 1)
        self.in_list(
            "Account2", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX),
            })
        self.in_list(
            "Account1", {
                "Key" : "2ndPoints",
                "Value" : str(self.VALUE_MAX - self.MAX + 1),
            })

    def test_move_accts_known_keys_defined_defined_INabove(self):
        """Moves above-range value from known account's defined key to other
        known's other key"""
        self.set_ex_success("Account2", "1rstPoints", self.VALUE_MAX, "", "")
        self.set_ex_success(
            "Account1", "2ndPoints", self.MAX + 1, "", "")
        self.move_overflow(
            "Account2", "1rstPoints", "Account1", "2ndPoints",
            self.UPPER_LIMIT)
        self.kvget_equal("Account2", "1rstPoints", self.VALUE_MAX)
        self.kvget_equal("Account1", "2ndPoints", self.MAX + 1)
        self.in_list(
            "Account2", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX),
            })
        self.in_list(
            "Account1", {
                "Key" : "2ndPoints",
                "Value" : str(self.MAX + 1),
            })

    def test_move_accts_known_keys_defined_defined_INbelow(self):
        """Tries moving a below-range value from one account's defined key
        to another acount's different defined key"""
        self.set_ex_success("Account2", "1rstPoints", self.VALUE_MAX, "", "")
        self.set_ex_success(
            "Account1", "2ndPoints", self.MAX + 1, "", "")
        self.move_overflow(
            "Account2", "1rstPoints", "Account1", "2ndPoints",
            self.LOWER_LIMIT)
        self.kvget_equal("Account2", "1rstPoints", self.VALUE_MAX)
        self.kvget_equal("Account1", "2ndPoints", self.MAX + 1)
        self.in_list(
            "Account2", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX),
            })
        self.in_list(
            "Account1", {
                "Key" : "2ndPoints",
                "Value" : str(self.MAX + 1),
            })

    def test_move_accts_known_keys_defined_undefined_INin_TARGin_SRCin(self):
        """Moves in-range value from known account's defined key to other
        known's undefined key; target in-range, source in-range if committed"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "UndefinedPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "UndefinedPoints", 1)
        self.lists_equal("Account2")
        self.in_list("Account1", self.UNKNOWN_LOCKED)

    def test_move_accts_known_keys_defined_undefined_INin_TARGin_SRC0(self):
        """Moves in-range value from known account's defined key to other
        known's undefined key; target in-range, source zero if committed"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "UndefinedPoints", 9)
        self.confirm_locked("Account2", "1rstPoints", 0)
        self.confirm_locked("Account1", "UndefinedPoints", 9)
        self.lists_equal("Account2")
        self.in_list("Account1", self.UNKNOWN_LOCKED)

    def test_move_accts_known_keys_defined_undefined_INin_TARGin_SRCbel(self):
        """Moves in-range value from known account's defined key to other
        known's undefined key; target in-range, source below-range if
        committed"""
        self.move_null(
            "Account2", "1rstPoints", "Account1", "UndefinedPoints", 10)
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.lists_equal("Account2")
        self.lists_equal("Account1")    

    def test_move_accts_known_keys_defined_undefined_INmax_TARGin_SRCin(self):
        """Moves max value from known account's defined key to other
        known's other key; target in-range, source in-range if committed"""
        self.set_ex_success("Account2", "1rstPoints", self.VALUE_MAX, "", "")
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "UndefinedPoints", self.MAX)
        self.confirm_locked(
            "Account2", "1rstPoints", self.VALUE_MAX - self.MAX)
        self.confirm_locked("Account1", "UndefinedPoints", self.MAX)
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.VALUE_MAX)})

    def test_move_accts_known_keys_defined_undefined_INmax_TARGin_SRC0(self):
        """Moves max value from known account's defined key to other
        known's other key; target in-range, source zero if committed"""
        self.set_ex_success("Account2", "1rstPoints", self.MAX, "", "")
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "UndefinedPoints", self.MAX)
        self.confirm_locked("Account2", "1rstPoints", 0)
        self.confirm_locked("Account1", "UndefinedPoints", self.MAX)
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.MAX)})

    def test_move_accts_known_keys_defined_undefined_INmax_TARGin_SRCbel(self):
        """Moves max value from known account's defined key to other
        known's other key; target in-range, source below-range if committed"""
        self.set_ex_success("Account2", "1rstPoints", self.MAX - 10, "", "")
        self.move_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", self.MAX)
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.kvget_equal("Account2", "1rstPoints", self.MAX - 10)
        self.lists_equal("Account1")
        self.in_list(
            "Account2", {"Key" : "1rstPoints", "Value" : str(self.MAX - 10)})

    def test_move_accts_known_keys_defined_undefined_INzero(self):
        """Moves zero from known account's defined key to other known's
        undefined key"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "UndefinedPoints", 0)
        self.confirm_locked("Account2", "1rstPoints", 9)
        self.confirm_locked("Account1", "UndefinedPoints", 0)
        self.lists_equal("Account2")
        self.in_list("Account1", self.UNKNOWN_LOCKED)

    def test_move_accts_known_keys_defined_undefined_INabove(self):
        """Moves above-range value from defined key of known acct to undefined
        key of other known acct"""
        self.set_ex_success("Account2", "1rstPoints", self.VALUE_MAX, "", "")
        self.move_overflow(
            "Account2", "1rstPoints", "Account1", "UndefinedPoints",
            self.UPPER_LIMIT)
        self.kvget_equal("Account2", "1rstPoints", self.VALUE_MAX)
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.in_list(
            "Account2", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX),
            })
        self.lists_equal("Account1")

    def test_move_accts_known_keys_defined_undefined_INbelow(self):
        """Moves below-range value from known account's defined key to other
        known's other key"""
        self.set_ex_success("Account2", "1rstPoints", self.VALUE_MAX, "", "")
        self.move_overflow(
            "Account2", "1rstPoints", "Account1", "UndefinedPoints",
            self.LOWER_LIMIT)
        self.kvget_equal("Account2", "1rstPoints", self.VALUE_MAX)
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.in_list(
            "Account2", {
                "Key" : "1rstPoints",
                "Value" : str(self.VALUE_MAX),
            })
        self.lists_equal("Account1")

    def test_move_accts_known_keys_undefined_defined(self):
        """Moves undefined key of known acct to defined key of other known
        acct"""
        self.move_null(
            "Account1", "UndefinedPoints", "Account2", "1rstPoints", 1)
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.lists_equal("Account1")
        self.lists_equal("Account2")

    def test_mvrollback_acct_defined_defined_first(self):
        """Move key 1 to key 2 on same account, rollback first key"""
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "2ndPoints", 1)
        self.confirm_locked("Account1", "1rstPoints", 8)
        self.confirm_locked("Account1", "2ndPoints", 10)
        self.lists_equal("Account1")
        self.kvrollback_success("Account1", "1rstPoints")
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.kvget_equal("Account1", "2ndPoints", 9)
        self.lists_equal("Account1")

    def test_mvrollback_acct_defined_defined_second(self):
        """Move key 1 to key 2 on same account, rollback second key"""
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "2ndPoints", 1)
        self.confirm_locked("Account1", "1rstPoints", 8)
        self.confirm_locked("Account1", "2ndPoints", 10)
        self.lists_equal("Account1")
        self.kvrollback_success("Account1", "2ndPoints")
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.kvget_equal("Account1", "2ndPoints", 9)
        self.lists_equal("Account1")

    def test_mvrollback_acct_defined_undefined_first(self):
        """Move key 1 to undef key on same account, rollback first key"""
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "UndefinedPoints", 1)
        self.confirm_locked("Account1", "1rstPoints", 8)
        self.confirm_locked("Account1", "UndefinedPoints", 1)
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.kvrollback_success("Account1", "1rstPoints")
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.lists_equal("Account1")

    def test_mvrollback_acct_defined_undefined_second(self):
        """Move key 1 to undef key on same account, rollback second key"""
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "UndefinedPoints", 1)
        self.confirm_locked("Account1", "1rstPoints", 8)
        self.confirm_locked("Account1", "UndefinedPoints", 1)
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.kvrollback_success("Account1", "UndefinedPoints")
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.lists_equal("Account1")

    def test_mvrollback_accts_same_key_first(self):
        """Move key1 on acct1 to key1 on acct2, rollback first key"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "1rstPoints", 10)
        self.lists_equal("Account2")
        self.lists_equal("Account1")
        self.kvrollback_success("Account2", "1rstPoints")
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_mvrollback_accts_same_key_second(self):
        """Move key1 on acct1 to key1 on acct2, rollback second key"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "1rstPoints", 10)
        self.lists_equal("Account2")
        self.lists_equal("Account1")
        self.kvrollback_success("Account1", "1rstPoints")
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.kvget_equal("Account1", "1rstPoints", 9)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_mvrollback_accts_defined_defined_first(self):
        """Move key1 on acct1 to key2 on acct2, rollback first key"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "2ndPoints", 10)
        self.lists_equal("Account2")
        self.lists_equal("Account1")
        self.kvrollback_success("Account2", "1rstPoints")
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.kvget_equal("Account1", "2ndPoints", 9)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_mvrollback_accts_defined_defined_second(self):
        """Move key1 on acct1 to key2 on acct2, rollback second key"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "2ndPoints", 10)
        self.lists_equal("Account2")
        self.lists_equal("Account1")
        self.kvrollback_success("Account1", "2ndPoints")
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.kvget_equal("Account1", "2ndPoints", 9)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_mvrollback_accts_defined_undefined_first(self):
        """Move key1 on acct1 to undefined key on acct2, rollback first"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "UndefinedPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "UndefinedPoints", 1)
        self.lists_equal("Account2")
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.kvrollback_success("Account2", "1rstPoints")
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_mvrollback_accts_defined_undefined_second(self):
        """Move key1 on acct1 to undefined key on acct2, rollback second"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "UndefinedPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "UndefinedPoints", 1)
        self.lists_equal("Account2")
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.kvrollback_success("Account1", "UndefinedPoints")
        self.kvget_equal("Account2", "1rstPoints", 9)
        self.kvget_equal("Account1", "UndefinedPoints", -1)
        self.lists_equal("Account2")
        self.lists_equal("Account1")

    def test_mvcommit_acct_defined_defined_first(self):
        """Move key 1 to key 2 on same account, commit first"""
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "2ndPoints", 1)
        self.confirm_locked("Account1", "1rstPoints", 8)
        self.confirm_locked("Account1", "2ndPoints", 10)
        self.lists_equal("Account1")
        self.kvcommit_success("Account1", "1rstPoints")
        self.kvget_equal("Account1", "1rstPoints", 8)
        self.kvget_equal("Account1", "2ndPoints", 10)
        self.in_list("Account1", {"Value" : "8", "Key" : "1rstPoints"})
        self.in_list("Account1", {"Value" : "10", "Key" : "2ndPoints"})

    def test_mvcommit_acct_defined_defined_second(self):
        """Move key 1 to key 2 on same account, commit second"""
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "2ndPoints", 1)
        self.confirm_locked("Account1", "1rstPoints", 8)
        self.confirm_locked("Account1", "2ndPoints", 10)
        self.lists_equal("Account1")
        self.kvcommit_success("Account1", "2ndPoints")
        self.kvget_equal("Account1", "1rstPoints", 8)
        self.kvget_equal("Account1", "2ndPoints", 10)
        self.in_list("Account1", {"Value" : "8", "Key" : "1rstPoints"})
        self.in_list("Account1", {"Value" : "10", "Key" : "2ndPoints"})

    def test_mvcommit_acct_defined_undefined_first(self):
        """"Move key 1 to undefined key on same account, commit first"""
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "UndefinedPoints", 1)
        self.confirm_locked("Account1", "1rstPoints", 8)
        self.confirm_locked("Account1", "UndefinedPoints", 1)
        self.in_list("Account1", {"Value" : "9", "Key" : "1rstPoints"})
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.kvcommit_success("Account1", "1rstPoints")
        self.kvget_equal("Account1", "1rstPoints", 8)
        self.kvget_equal("Account1", "UndefinedPoints", 1)
        self.in_list("Account1", {"Value" : "8", "Key" : "1rstPoints"})
        self.in_list("Account1", {"Value" : "1", "Key" : "UndefinedPoints"})

    def test_mvcommit_acct_defined_undefined_second(self):
        """"Move key 1 to undefined key on same account, commit second"""
        self.move_not_null(
            "Account1", "1rstPoints", "Account1", "UndefinedPoints", 1)
        self.confirm_locked("Account1", "1rstPoints", 8)
        self.confirm_locked("Account1", "UndefinedPoints", 1)
        self.in_list("Account1", {"Value" : "9", "Key" : "1rstPoints"})
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.kvcommit_success("Account1", "1rstPoints")
        self.kvget_equal("Account1", "1rstPoints", 8)
        self.kvget_equal("Account1", "UndefinedPoints", 1)
        self.in_list("Account1", {"Value" : "8", "Key" : "1rstPoints"})
        self.in_list("Account1", {"Value" : "1", "Key" : "UndefinedPoints"})

    def test_mvcommit_accts_same_key_first(self):
        """Move key1 on acct1 to key1 on acct2, commit first"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "1rstPoints", 10)
        self.lists_equal("Account2")
        self.lists_equal("Account1")
        self.kvcommit_success("Account2", "1rstPoints")
        self.kvget_equal("Account2", "1rstPoints", 8)
        self.kvget_equal("Account1", "1rstPoints", 10)
        self.in_list("Account2", {"Value" : "8", "Key" : "1rstPoints"})
        self.in_list("Account1", {"Value" : "10", "Key" : "1rstPoints"})

    def test_mvcommit_accts_same_key_second(self):
        """Move key1 on acct1 to key1 on acct2, commit second"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "1rstPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "1rstPoints", 10)
        self.lists_equal("Account2")
        self.lists_equal("Account1")
        self.kvcommit_success("Account1", "1rstPoints")
        self.kvget_equal("Account2", "1rstPoints", 8)
        self.kvget_equal("Account1", "1rstPoints", 10)
        self.in_list("Account2", {"Value" : "8", "Key" : "1rstPoints"})
        self.in_list("Account1", {"Value" : "10", "Key" : "1rstPoints"})

    def test_mvcommit_accts_defined_defined_first(self):
        """Move key1 on acct1 to key2 on acct2, commit first"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "2ndPoints", 10)
        self.lists_equal("Account2")
        self.lists_equal("Account1")
        self.kvcommit_success("Account2", "1rstPoints")
        self.kvget_equal("Account2", "1rstPoints", 8)
        self.kvget_equal("Account1", "2ndPoints", 10)
        self.in_list("Account2", {"Value" : "8", "Key" : "1rstPoints"})
        self.in_list("Account1", {"Value" : "10", "Key" : "2ndPoints"})

    def test_mvcommit_accts_defined_defined_second(self):
        """Move key1 on acct1 to key2 on acct2, commit second"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "2ndPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "2ndPoints", 10)
        self.lists_equal("Account2")
        self.lists_equal("Account1")
        self.kvcommit_success("Account1", "2ndPoints")
        self.kvget_equal("Account2", "1rstPoints", 8)
        self.kvget_equal("Account1", "2ndPoints", 10)
        self.in_list("Account2", {"Value" : "8", "Key" : "1rstPoints"})
        self.in_list("Account1", {"Value" : "10", "Key" : "2ndPoints"})

    def test_mvcommit_accts_defined_undefined_first(self):
        """Move key1 on acct1 to undefined key on acct2, commit first"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "UndefinedPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "UndefinedPoints", 1)
        self.lists_equal("Account2")
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.kvcommit_success("Account2", "1rstPoints")
        self.kvget_equal("Account2", "1rstPoints", 8)
        self.kvget_equal("Account1", "UndefinedPoints", 1)
        self.in_list("Account2", {"Value" : "8", "Key" : "1rstPoints"})
        self.in_list("Account1", {"Value" : "1", "Key" : "UndefinedPoints"})

    def test_mvcommit_accts_defined_undefined_second(self):
        """Move key1 on acct1 to undefined key on acct2, commit second"""
        self.move_not_null(
            "Account2", "1rstPoints", "Account1", "UndefinedPoints", 1)
        self.confirm_locked("Account2", "1rstPoints", 8)
        self.confirm_locked("Account1", "UndefinedPoints", 1)
        self.lists_equal("Account2")
        self.in_list("Account1", self.UNKNOWN_LOCKED)
        self.kvcommit_success("Account1", "UndefinedPoints")
        self.kvget_equal("Account2", "1rstPoints", 8)
        self.kvget_equal("Account1", "UndefinedPoints", 1)
        self.in_list("Account2", {"Value" : "8", "Key" : "1rstPoints"})
        self.in_list("Account1", {"Value" : "1", "Key" : "UndefinedPoints"})

    def test_move_is_lock_by_rollback_once(self):
        accts = []
        accts.append(["Account2", "1rstPoints", 8, 9])
        accts.append(["Account1", "1rstPoints", 10, 9])
        self.assertIsInstance(accts[1][0], str)
        self.move_not_null(
            accts[0][0], accts[0][1], accts[1][0], accts[1][1], 1)
        self.confirm_locked(accts[0][0], accts[0][1], accts[0][2])
        self.confirm_locked(accts[1][0], accts[1][1], accts[1][2])
        i = random.randint(0, 1)
        j = (i + 1) % 2
        self.kvrollback_success(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][3])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][3])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][3])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][3])})
        self.not_null_lock(accts[j][0], accts[j][1])
        self.confirm_locked(accts[j][0], accts[j][1], accts[j][3])
        self.not_null_lock(accts[i][0], accts[i][1])
        self.confirm_locked(str(accts[i][0]), str(accts[i][1]), accts[i][3])

    def test_move_is_lock_by_commit_once(self):
        accts = []
        accts.append(["Account2", "1rstPoints", 8, 9])
        accts.append(["Account1", "1rstPoints", 10, 9])
        self.move_not_null(
            accts[0][0], accts[0][1], accts[1][0], accts[1][1], 1)
        self.confirm_locked(accts[0][0], accts[0][1], accts[0][2])
        self.confirm_locked(accts[1][0], accts[1][1], accts[1][2])
        i = random.randint(0, 1)
        j = (i + 1) % 2
        self.kvcommit_success(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][2])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][2])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][2])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][2])})
        self.not_null_lock(accts[j][0], accts[j][1])
        self.confirm_locked(accts[j][0], accts[j][1], accts[j][2])
        self.not_null_lock(accts[i][0], accts[i][1])
        self.confirm_locked(str(accts[i][0]), str(accts[i][1]), accts[i][2])

    def test_move_is_lock_by_double_rollback_same(self):
        accts = []
        accts.append(["Account2", "1rstPoints", 8, 9])
        accts.append(["Account1", "1rstPoints", 10, 9])
        self.move_not_null(
            accts[0][0], accts[0][1], accts[1][0], accts[1][1], 1)
        self.confirm_locked(accts[0][0], accts[0][1], accts[0][2])
        self.confirm_locked(accts[1][0], accts[1][1], accts[1][2])
        i = random.randint(0, 1)
        j = (i + 1) % 2
        self.kvrollback_success(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][3])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][3])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][3])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][3])})
        self.kvrollback_fail(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][3])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][3])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][3])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][3])})
        self.not_null_lock(accts[j][0], accts[j][1])
        self.confirm_locked(accts[j][0], accts[j][1], accts[j][3])
        self.not_null_lock(accts[i][0], accts[i][1])
        self.confirm_locked(str(accts[i][0]), str(accts[i][1]), accts[i][3])

    def test_move_is_lock_by_double_rollback_opp(self):
        accts = []
        accts.append(["Account2", "1rstPoints", 8, 9])
        accts.append(["Account1", "1rstPoints", 10, 9])
        self.assertIsInstance(accts[1][0], str)
        self.move_not_null(
            accts[0][0], accts[0][1], accts[1][0], accts[1][1], 1)
        self.confirm_locked(accts[0][0], accts[0][1], accts[0][2])
        self.confirm_locked(accts[1][0], accts[1][1], accts[1][2])
        i = random.randint(0, 1)
        j = (i + 1) % 2
        self.kvrollback_success(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][3])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][3])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][3])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][3])})
        self.kvrollback_fail(accts[j][0], accts[j][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][3])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][3])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][3])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][3])})
        self.not_null_lock(accts[j][0], accts[j][1])
        self.confirm_locked(accts[j][0], accts[j][1], accts[j][3])
        self.not_null_lock(accts[i][0], accts[i][1])
        self.confirm_locked(str(accts[i][0]), str(accts[i][1]), accts[i][3])

    def test_move_is_lock_by_double_commit_same(self):
        accts = []
        accts.append(["Account2", "1rstPoints", 8, 9])
        accts.append(["Account1", "1rstPoints", 10, 9])
        self.assertIsInstance(accts[1][0], str)
        self.move_not_null(
            accts[0][0], accts[0][1], accts[1][0], accts[1][1], 1)
        self.confirm_locked(accts[0][0], accts[0][1], accts[0][2])
        self.confirm_locked(accts[1][0], accts[1][1], accts[1][2])
        i = random.randint(0, 1)
        j = (i + 1) % 2
        self.kvcommit_success(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][2])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][2])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][2])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][2])})
        self.kvcommit_fail(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][2])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][2])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][2])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][2])})
        self.not_null_lock(accts[j][0], accts[j][1])
        self.confirm_locked(accts[j][0], accts[j][1], accts[j][2])
        self.not_null_lock(accts[i][0], accts[i][1])
        self.confirm_locked(str(accts[i][0]), str(accts[i][1]), accts[i][2])

    def test_move_is_lock_by_double_commit_opp(self):
        accts = []
        accts.append(["Account2", "1rstPoints", 8, 9])
        accts.append(["Account1", "1rstPoints", 10, 9])
        self.assertIsInstance(accts[1][0], str)
        self.move_not_null(
            accts[0][0], accts[0][1], accts[1][0], accts[1][1], 1)
        self.confirm_locked(accts[0][0], accts[0][1], accts[0][2])
        self.confirm_locked(accts[1][0], accts[1][1], accts[1][2])
        i = random.randint(0, 1)
        j = (i + 1) % 2
        self.kvcommit_success(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][2])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][2])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][2])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][2])})
        self.kvcommit_fail(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][2])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][2])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][2])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][2])})
        self.not_null_lock(accts[j][0], accts[j][1])
        self.confirm_locked(accts[j][0], accts[j][1], accts[j][2])
        self.not_null_lock(accts[i][0], accts[i][1])
        self.confirm_locked(str(accts[i][0]), str(accts[i][1]), accts[i][2])

    def test_move_is_lock_by_rollback_commit_same(self):
        accts = []
        accts.append(["Account2", "1rstPoints", 8, 9])
        accts.append(["Account1", "1rstPoints", 10, 9])
        self.move_not_null(
            accts[0][0], accts[0][1], accts[1][0], accts[1][1], 1)
        self.confirm_locked(accts[0][0], accts[0][1], accts[0][2])
        self.confirm_locked(accts[1][0], accts[1][1], accts[1][2])
        i = random.randint(0, 1)
        j = (i + 1) % 2
        self.kvrollback_success(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][3])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][3])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][3])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][3])})
        self.kvcommit_fail(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][3])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][3])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][3])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][3])})
        self.not_null_lock(accts[j][0], accts[j][1])
        self.confirm_locked(accts[j][0], accts[j][1], accts[j][3])
        self.not_null_lock(accts[i][0], accts[i][1])
        self.confirm_locked(str(accts[i][0]), str(accts[i][1]), accts[i][3])

    def test_move_is_lock_by_rollback_commit_opp(self):
        accts = []
        accts.append(["Account2", "1rstPoints", 8, 9])
        accts.append(["Account1", "1rstPoints", 10, 9])
        self.assertIsInstance(accts[1][0], str)
        self.move_not_null(
            accts[0][0], accts[0][1], accts[1][0], accts[1][1], 1)
        self.confirm_locked(accts[0][0], accts[0][1], accts[0][2])
        self.confirm_locked(accts[1][0], accts[1][1], accts[1][2])
        i = random.randint(0, 1)
        j = (i + 1) % 2
        self.kvrollback_success(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][3])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][3])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][3])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][3])})
        self.kvcommit_fail(accts[j][0], accts[j][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][3])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][3])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][3])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][3])})
        self.not_null_lock(accts[j][0], accts[j][1])
        self.confirm_locked(accts[j][0], accts[j][1], accts[j][3])
        self.not_null_lock(accts[i][0], accts[i][1])
        self.confirm_locked(str(accts[i][0]), str(accts[i][1]), accts[i][3])

    def test_move_is_lock_by_commit_rollback_same(self):
        accts = []
        accts.append(["Account2", "1rstPoints", 8, 9])
        accts.append(["Account1", "1rstPoints", 10, 9])
        self.assertIsInstance(accts[1][0], str)
        self.move_not_null(
            accts[0][0], accts[0][1], accts[1][0], accts[1][1], 1)
        self.confirm_locked(accts[0][0], accts[0][1], accts[0][2])
        self.confirm_locked(accts[1][0], accts[1][1], accts[1][2])
        i = random.randint(0, 1)
        j = (i + 1) % 2
        self.kvcommit_success(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][2])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][2])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][2])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][2])})
        self.kvrollback_fail(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][2])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][2])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][2])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][2])})
        self.not_null_lock(accts[j][0], accts[j][1])
        self.confirm_locked(accts[j][0], accts[j][1], accts[j][2])
        self.not_null_lock(accts[i][0], accts[i][1])
        self.confirm_locked(str(accts[i][0]), str(accts[i][1]), accts[i][2])

    def test_move_is_lock_by_commit_rollback_opp(self):
        accts = []
        accts.append(["Account2", "1rstPoints", 8, 9])
        accts.append(["Account1", "1rstPoints", 10, 9])
        self.assertIsInstance(accts[1][0], str)
        self.move_not_null(
            accts[0][0], accts[0][1], accts[1][0], accts[1][1], 1)
        self.confirm_locked(accts[0][0], accts[0][1], accts[0][2])
        self.confirm_locked(accts[1][0], accts[1][1], accts[1][2])
        i = random.randint(0, 1)
        j = (i + 1) % 2
        self.kvcommit_success(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][2])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][2])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][2])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][2])})
        self.kvrollback_fail(accts[i][0], accts[i][1])
        self.kvget_equal(accts[i][0], accts[i][1], accts[i][2])
        self.in_list(
            accts[i][0], {"Key" : accts[i][1], "Value" : str(accts[i][2])})
        self.kvget_equal(accts[j][0], accts[j][1], accts[j][2])
        self.in_list(
            accts[j][0], {"Key" : accts[j][1], "Value" : str(accts[j][2])})
        self.not_null_lock(accts[j][0], accts[j][1])
        self.confirm_locked(accts[j][0], accts[j][1], accts[j][2])
        self.not_null_lock(accts[i][0], accts[i][1])
        self.confirm_locked(str(accts[i][0]), str(accts[i][1]), accts[i][2])


if __name__ == '__main__':
    unittest.main()
