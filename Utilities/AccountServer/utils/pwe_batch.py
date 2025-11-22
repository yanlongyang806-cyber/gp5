# For help, type "python pwe_batch.py --help"

from optparse import OptionParser
import sys
import csv
import getpass
from xmlrpclib import ServerProxy
from threaded_iter import threaded_iter


def iter_pwe_batch(host, account_names, num_threads=1):
    """ Iterate over an array of xpaths with an array of args """
    def iter_func(account_name):
        proxy = ServerProxy('http://%s/xmlrpc' % host)
        return (account_name, proxy.TranslatePWAccountNameToBatchID(account_name))
    return threaded_iter(iter_func, [(x,) for x in account_names], num_threads)


if __name__ == '__main__':
    in_file = sys.stdin
    out_file = sys.stdout
    parser = OptionParser("usage: %prog [options] host")
    parser.add_option('-t', '--threads', dest='threads',
            default=1, type='int', help='number of threads [default: %default]')
    parser.add_option('-u', '--user', dest='user',
            default=None, type='str', help='auth with username (will prompt for password)')
    (options, args) = parser.parse_args()
    if len(args) < 1:
        parser.error("incorrect number of arguments")

    host = args[0]
    if options.user:
        password = getpass.getpass('Password:')
        host = '%s:%s@%s' % (options.user, password, host)

    account_names = []
    argreader = csv.reader(in_file)
    for row in argreader:
        account_names.append(row[0])

    writer = csv.writer(out_file)
    writer.writerow(['account_name', 'pwe_batch'])
    for account_name, batch_id in iter_pwe_batch(
            host=host,
            account_names=account_names,
            num_threads=options.threads):

        row_elems = [account_name, batch_id]
        writer.writerow(row_elems)
