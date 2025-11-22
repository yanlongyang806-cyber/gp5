from __future__ import with_statement
##intended for compatability with Python v2.5; blocked by 'as' in proxy.py
from threading import Thread,Lock
import proxy
import random
from proxy import ServiceProxy
import json
import string
import sys
import time
import copy
import urllib2
import httplib
import pickle
import socket


mutex = Lock()
threads = []
test_output = {
        "attempt" : 0,
        "success" : 0,
        "failure" : 0,
        "Other" : 0,
        "URLError" : 0,
        "BadStatusLine" : 0,
        "HTTPError" : 0,
}
real_keys = ["CrypticEmployee",
             "NeverwinterChain",
             "promoStarTrekzen",
             "promoChampionszen",
             "NeverwinterEscrowChain",
             "ChampionsEscrowChain",
             "StarTrekEscrowChain",
             "NW.UgcProjectSearchEULA",
]
tutorial_keys = [
    "game_status",
    "ip_address",
    "last_login",
    "response",
]
command_list = [
    "available_account",
    "no_action_available_account",
    "request_account",
    "no_action_request_account",
    "update_account",
    "no_action_update_account",
    "transfer_currency",
    "no_action_transfer_currency",
    "check_tutorial_finished",
    "no_action_check_tutorial_finished",
    "validate_login",
    "no_action_validate_login",
    "no_action_trigger_email_event",
    "trigger_email_event",
]
accounts = [
    (
        "acctexists",
        "acctexistshdl",
        "testaccountexists@crypticstudios.com",
        "A0HASH",
        0,
    ),
    (
        "notheracctexists",
        "notheracctexistshdl",
        "testanotheraccountexists@crypticstudios.com",
        "0534E64B89C2CBE028DDE8B656F500C1", ### equivalent to password: 'test'
        0,
    ),
    (
        "nacctexists",
        "nacctexistshdl",
        "doesnotexist@crypticstudios.com",
        "ANOTHER1HASH",
        0,
    ),
]
currencies = [
    "PaidChampionsZen",
    "PaidNeverwinterZen",
    "PaidStarTrekZen",
]
accounts_length = len(accounts)
currencies_length = len(currencies)


def random_string(size):
    """Helper function returns random string of size characters"""
    chars = string.digits + string.ascii_letters
    return "".join(random.choice(chars) for x in range(size))

def available_account_request(self, info):
    """Takes info array; returns outcome of available_account request"""
    return self.proxy.Account.available_account(info[0])

def request_account_request(self, info):
    """Takes info array; returns outcome of request_account request"""
    return self.proxy.Account.request_account(info[0], info[1])

def check_tutorial_finished_request(self, info):
    """Takes info array; returns outcome of check_tutorial_finished"""
    return self.proxy.Account.check_tutorial_finished(info[0])

def transfer_currency_request(self, info):
    """Transfers an amount of random currency to specified account"""
    currency = currencies[random.randint(0, currencies_length - 1)]
    amount = random.randint(-1, 1)
    return self.proxy.Account.transfer_currency(info[0], currency,
                                                        amount)

def validate_login_request(self, info):
    """Attempts to login with specified account"""
    ip = socket.gethostbyname(socket.gethostname()) # Don't lie.
    loc = random_string(random.randint(4, 50))
    ref = random_string(random.randint(4, 50))
    client = random_string(random.randint(4, 50))
    http = random_string(random.randint(4,30))
    out = self.proxy.Account.validate_login(info[0],info[3],
                                             ip, loc, ref, client, http)
    return out

def trigger_email_event_request(self, info):
    """Trigger an email to be sent to the player"""
    recipients = info[2]
    event_name = 'AccountGuardEmail'
    return self.proxy.Email.trigger_email_event(recipients, event_name)

def update_account_request(self, info):
    """Attempts to update specified account; defined accounts only!"""
    return self.proxy.Account.update_account(info[0], info[1], info[2],
                                             info[3], info[4])

def no_action_exists_or_unf(self, info):
    """Do-nothing command, used to establish baseline"""
    return {"result" : {"response" : "user_exists"}, "error" : "BASELINE"}

def no_action_request_account(self, info):
    """Do-nothing command, used to establish baseline"""
    return {"result" : {"response" : "success"}, "error" : "BASELINE"}

def no_action_check_tutorial(self, info):
    """Do-nothing command, used to establish baseline"""
    result = {
        "game_status": 0,
        "ip_address" : 0,
        "last_login" : 0,
        "response" : True,
    }
    return {"result" : result, "error" : "BASELINE"}

def no_action_not_error(self, info):
    """Do-nothing command, used to establish baseline"""
    return {"result" : {"response" : "VOMIT"}, "error" : "BASELINE"}

def get_result(reply):
    """Tries to extract info from reply to request; returns result"""
    try:
        result = reply['result']
    except (TypeError, KeyError):
        error = reply['error']
        result = "ERROR"
    if result is "ERROR":
        if error:
            print error
        else:
            print result
    return result

def request_account_verify(self, reply):
    """Verifies output of request_account request"""
    result = get_result(reply)
    if (result['response'] == "success" or result['response'] ==
        "pwuser_unknown"):
        return True
    else:
        return False

def exists_or_unf_verify(self, reply):
    """Verifies output of requests which should return user_exists or unf"""
    result = get_result(reply)
    response = result['response']
    if response == "user_not_found" or response == "user_exists":
        return True
    else:
        return False

def check_tutorial_verify(self, reply):
    """Verifies output of check_tutorial_finished_request"""
    result = get_result(reply)
    if result == "ERROR" or result is None:
        return False
    elif(set(result.keys()) != set(tutorial_keys)):
        return False
    elif result['response'] is None:
        return False
    else:
        return True

def response_exists_verify(self, reply):
    """Verifies output of transfer_currency_request"""
    result = get_result(reply)
    if result == "ERROR" or result is None:
        return False
    elif result['response'] is None:
        return False
    else:
        return True

def trigger_email_event_verify(self, reply):
    """Verifies the result is not an error, even through response is empty"""
    try:
        error = reply['error']
        # Who cares?
        return True
    except (TypeError, KeyError):
        return False

def validate_login_verify(self, reply):
    """Verifies output of validate_login_request"""
    result = get_result(reply)
    if result is "ERROR" or result is None:
        return False
    else:
        return True

def update_account_verify(self, reply):
    """Verifies output of update_account_request"""
    result = get_result(reply)
    if result == "ERROR" or result == None:
        return False
    else:
        return True


class TestThread(Thread):
    """Thread object for JSON command loops"""
    def __init__(self, task, verify, repetitions, taskname, threadnumber,
                 group=None, target=None, name=None, verbose=None):
        """Class Constructor"""
        Thread.__init__(self, group=group, target=target, verbose=verbose)
        self.proxy = ServiceProxy(sys.argv[1])
        self.repetitions = repetitions
        self.task = task
        self.verify = verify
        self.taskname = taskname
        self.threadnumber = threadnumber

    def run(self):
        """All the commands this thread shall run!"""
        for each in range(self.repetitions):
            user_info = accounts[random.randint(0, accounts_length - 1)]
            try:
                ### This line executes and verifies all requests
                status = self.verify(self, self.task(self, user_info))
            except urllib2.URLError:
                bucket = "URLError"
            except httplib.BadStatusLine:
                bucket = "BadStatusLine"
            except urllib2.HTTPError:
                bucket = "HTTPError"
            else:
                if(status):
                    bucket = "success"
                else:
                    bucket = "Other"
            with mutex:
                # Send command line feedback to stderr.
                if(test_output["attempt"] % 10 == 0):
                    sys.stderr.write("  %d (%d)\r" % (test_output["attempt"],
                                                     test_output["failure"]))
                sys.stdout.flush()

                # Update test results.
                test_output["attempt"] += 1
                test_output[bucket]    += 1
                if(bucket != "success"):
                    test_output["failure"] += 1


if len(sys.argv) < 3 or len(sys.argv) > 6:
    print "Usage error. Program takes 3 to 6 arguments; "
    print str(len(sys.argv)) + " given.\n"
    print ("Usage:\t JSONWebSRVTest.py <targetenvironment> <test command>"
           + "<number of threads> <requests per thread> <picklepath>")
    sys.exit(1)
elif sys.argv[2] not in command_list:
    print "Error! Specified command not found!"
    sys.exit(2)
if len(sys.argv) > 3:
    thread_count = int(sys.argv[3])
else:
    thread_count = 5
if len(sys.argv) > 4:
    reps = int(sys.argv[4])
else:
    reps = 100
if len(sys.argv) > 5:
    picklepath = str(sys.argv[5])
else:
    picklepath = None
if str(sys.argv[2]).lower() == "available_account":
    command = available_account_request
    verify = exists_or_unf_verify
elif str(sys.argv[2]).lower() == "request_account":
    command = request_account_request
    verify = request_account_verify
elif str(sys.argv[2]).lower() == "check_tutorial_finished":
    command = check_tutorial_finished_request
    verify = check_tutorial_verify
elif str(sys.argv[2]).lower() == "transfer_currency":
    command = transfer_currency_request
    verify = response_exists_verify
elif str(sys.argv[2]).lower() == "validate_login":
    command = validate_login_request
    verify = response_exists_verify
elif str(sys.argv[2]).lower() == "update_account":
    accounts_length -= 1
    command = update_account_request
    verify = response_exists_verify
elif str(sys.argv[2].lower()) == "trigger_email_event":
    command = trigger_email_event_request
    verify = trigger_email_event_verify
elif str(sys.argv[2]).lower() == "no_action_available_account":
    command = no_action_exists_or_unf
    verify = exists_or_unf_verify
elif str(sys.argv[2]).lower() == "no_action_request_account":
    command = no_action_request_account
    verify = request_account_verify
elif str(sys.argv[2]).lower() == "no_action_check_tutorial_finished":
    command = no_action_check_tutorial
    verify = check_tutorial_verify
elif str(sys.argv[2]).lower() == "no_action_transfer_currency":
    command = no_action_not_error
    verify = response_exists_verify
elif str(sys.argv[2]).lower() == "no_action_validate_login":
    command = no_action_not_error
    verify = response_exists_verify
elif str(sys.argv[2]).lower() == "no_action_update_account":
    accounts_length -= 1
    command = no_action_not_error
    verify = response_exists_verify
elif str(sys.argv[2].lower()) == "no_action_trigger_email_event":
    command = no_action_not_error
    verify = response_exists_verify
else:
    print sys.argv[2]
    print "Command not yet implemented!"
    sys.exit(3)
for thread in range(thread_count):
    threads.append(TestThread(command, verify, reps, sys.argv[2], thread))
started_at = time.time()
for thread in range(thread_count):
    threads[thread].start()
for thread in range(thread_count):
    threads[thread].join()
ended_at = time.time()
duration = round(ended_at - started_at, 2)
print (
    "Command: %s"    % sys.argv[2] +
    ", Threads: %s"  % sys.argv[3] +
    ", Queries: %s"  % sys.argv[4] +
    ", Time: %0.2f"  % duration +
    ", Attempts: %d" % test_output["attempt"] +
    " (Success: %d"  % test_output["success"] +
    ", Errors: %d"   % test_output["failure"] +
        " (HTTPError: %d"     % test_output["HTTPError"] +
        ", URLError: %d"      % test_output["URLError"] +
        ", BadStatusLine: %d" % test_output["BadStatusLine"] +
        ", Other: %d"         % test_output["Other"] +
    "))"
)
if picklepath is not None:
    test_output["duration"] = duration
    f = open(picklepath, "w")
    pickle.dump(test_output, f)
    f.close()
sys.exit(0)
