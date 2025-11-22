# Script for creating PWE accounts in an Account Server
#
# Run with --help for usage info

from xmlrpclib import ServerProxy
from optparse import OptionParser
import sys
import time
import hashlib
import threading
import Queue
import socket


def print_version(account_server):
    """ Print the Account Server version """
    print 'Account Server version: ' + account_server.version()


def get_name(prefix, index):
    """ Generate an automatic name given a prefix and index """
    return prefix + '%06d' % index


def get_pw_hash(salt, password):
    """ Get a PWE password hash """
    m = hashlib.md5()
    m.update(salt)
    m.update(password)
    return m.hexdigest().upper()


def push_account(host, account_index, name_prefix, email_prefix, use_salt, fixed_pw):
    """ Push an account to the Account Server """
    account_server = ServerProxy('http://' + host + ':8081/xmlrpc')
    account_name = get_name(name_prefix, account_index)

    password = account_name
    if fixed_pw:
        password = 'password'

    request = {
        'AccountName': account_name,
        'ForumName': account_name,
        'Email': get_name(email_prefix, account_index),
        'PasswordHash': get_pw_hash(account_name, password),
        'FixedSalt': '',
        'PasswordHashFixedSalt': '',
    }
    if use_salt:
        security_code = str(account_index)
        request['FixedSalt'] = security_code
        request['PasswordHashFixedSalt'] = get_pw_hash(security_code, password)
    result = getattr(account_server, 'PW::UpdateAccount')(request)
    assert(result['Result'] == 'success')


def link_account(host, account_index, name_prefix):
    """ Link an existing PWE account to a new shadow Cryptic account """
    account_server = ServerProxy('http://' + host + ':8081/xmlrpc')
    result = account_server.AutoCreateAccountFromPW({
        'PWAccountName': get_name(name_prefix, account_index),
    })
    assert(result['UserStatus'] == 'success')


class CreateAccountThread(threading.Thread):

    def __init__(self, queue, host, link, name_prefix, email_prefix, use_salt, fixed_pw):
        super(CreateAccountThread, self).__init__()
        self.queue = queue
        self.link = link
        self.host = host
        self.name_prefix = name_prefix
        self.email_prefix = email_prefix
        self.use_salt = use_salt
        self.fixed_pw = fixed_pw
        self.sleeping = False

    def run(self):
        cur_account = None
        while True:
            if not cur_account:
                cur_account = self.queue.get()
            if not cur_account:
                break

            try:
                if self.link:
                    link_account(self.host, cur_account, self.name_prefix)
                else:
                    push_account(self.host, cur_account, self.name_prefix, self.email_prefix, self.use_salt, self.fixed_pw)

                self.queue.task_done()
                cur_account = None
            except socket.error as e:
                self.sleeping = True
                time.sleep(1)
                self.sleeping = False

    def sleeping(self):
        return self.sleeping


class StatusThread(threading.Thread):

    def __init__(self, queue, worker_threads):
        super(StatusThread, self).__init__()
        self.queue = queue
        self.highmark = 0
        self.worker_threads = worker_threads
        self.done = False

    def run(self):
        done = False
        while not done:
            if self.queue.empty() and self.highmark:
                done = True

            self.size = self.queue.qsize()
            if self.highmark < self.size:
                self.highmark = self.size

            self.update_status()
            if self.done:
                break

    def update_status(self):
        bar_length = 30
        if not self.highmark:
            return
        if self.size > 0:
            percent = 1.0 - float(self.size) / float(self.highmark)
        else:
            percent = 1.0
        hashes = '#' * int(round(percent * bar_length))
        spaces = ' ' * (bar_length - len(hashes))
        sleeping = 0
        for worker in self.worker_threads:
            if worker.sleeping:
                sleeping += 1
        sys.stdout.write('\rPercent: [%s] %d%% T:%d S:%d D:%d   ' %
                (hashes + spaces, int(round(percent * 100.0)), len(self.worker_threads), sleeping, self.highmark - self.size))
        if self.size == 0:
            self.done = True
            sys.stdout.write('\n')
        sys.stdout.flush()


def create_pwe_accounts(host, num_threads, num_accounts, name_prefix, email_prefix, start_index, link, use_salt, fixed_pw):
    """ Connect to an Account Server and create a bunch of PWE accounts """
    account_server = ServerProxy('http://' + host + ':8081/xmlrpc')
    print_version(account_server)
    print 'Creating %d PWE accounts' % num_accounts
    queue = Queue.Queue()

    for cur_account in range(start_index, start_index + num_accounts):
        queue.put(cur_account)

    worker_threads = []
    for i in range(num_threads):
        worker = CreateAccountThread(queue, host, link, name_prefix, email_prefix, use_salt, fixed_pw)
        worker.setDaemon(True)
        worker_threads.append(worker)

    status_thread = StatusThread(queue, worker_threads)
    status_thread.start()

    start_time = time.time()

    for worker in worker_threads:
        worker.start()

    queue.join()
    end_time = time.time()

    print 'Total time: %0.0fms'%((end_time - start_time)*1000)
    print 'Per creation: %0.0fms'%((end_time - start_time)*1000/num_accounts)


if __name__ == '__main__':
    parser = OptionParser('usage: %prog [options] host')
    parser.add_option('-n', '--num-accounts', dest='num_accounts',
            default=1000, type='int', help='number of accounts to create [default: %default]')
    parser.add_option('-p', '--name-prefix', dest='name_prefix',
            default='autocreate_', type='string', help='prefix used for account names [default: %default]')
    parser.add_option('-e', '--email-prefix', dest='email_prefix',
            default='autocreate@', type='string', help='prefix used for e-mail addresses [default: %default]')
    parser.add_option('-s', '--start-index', dest='start_index',
            default=1, type='int', help='starting index [default: %default]')
    parser.add_option('-l', '--link', dest='link',
            action='store_true', default=False, help='link the accounts to shadow Cryptic accounts')
    parser.add_option('-f', '--fixed-salt', dest='salt',
            action='store_true', default=False, help='include a fixed salt and hash')
    parser.add_option('-w', '--fixed-password', dest='password',
            action='store_true', default=False, help='allways use the password \'password\'')
    parser.add_option('-t', '--threads', dest='threads',
            default=1, type='int', help='number of threads [default: %default]')
    (options, args) = parser.parse_args()
    if len(args) != 1:
        parser.error('incorrect number of arguments')
    else:
        create_pwe_accounts(args[0], options.threads, options.num_accounts, options.name_prefix,
                options.email_prefix, options.start_index, options.link, options.salt, options.password)
