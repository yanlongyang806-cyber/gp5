# Script for removing a product from a list of accounts
#
# Run with --help for usage info

from xmlrpclib import ServerProxy
from optparse import OptionParser
import sys
import time


def print_version(account_server):
    """ Print the Account Server version """
    print 'Account Server version: ' + account_server.version()


def remove_product(account_server, account_name, product):
    """ Removes product from account """
    result = account_server.TakeProduct(account_name, product)
    if result['Result'] != 'product_taken':
        print 'Could not take from: ' + account_name


def remove_products(host, product, account_list_file):
    """ Connect to an Account Server and remove a product from accounts """
    account_server = ServerProxy('http://' + host + ':8081/xmlrpc')
    print_version(account_server)

    accounts = [i.strip() for i in open(account_list_file).readlines()]

    print 'Removing %s from %d accounts' % (product, len(accounts))

    start_time = time.time()
    for account in accounts:
        remove_product(account_server, account, product)
    end_time = time.time()

    print 'Total time: %0.0fms'%((end_time - start_time)*1000)
    print 'Per removal: %0.0fms'%((end_time - start_time)*1000/len(accounts))


if __name__ == '__main__':
    parser = OptionParser('usage: %prog [options] host product account_list_file')
    (options, args) = parser.parse_args()
    if len(args) != 3:
        parser.error('incorrect number of arguments')
    else:
        remove_products(args[0], args[1], args[2])
