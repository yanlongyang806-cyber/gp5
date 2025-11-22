# Script used to check a list of users for whether or not their password
# or Account Guard machine IDs have been changed
#
# Execute with --help for options

import xmlrpclib
from optparse import OptionParser
from getpass import getpass
import sys
from datetime import datetime


VERSION = "1.0"
SS_TO_2000 = 946684800


def check_user(proxy, user):
    """ checks a single user against the Account Server """
    res = proxy.UserInfo(user,
            (1 << 17)) # Include Account Guard machines
    if 'Savedclients' in res:
        for client in res['Savedclients']:
            t = client['Ulastseentime'] + SS_TO_2000
            dt = datetime.fromtimestamp(t)
            print user + ',' + \
                  client['ip'] + ',' + \
                  client['Pmachinename'] + ',' + \
                  client['Pmachineid'] + ',' + \
                  str(dt) + ',' + \
                  str(res['Machinelockenabled'])


def make_connection(user, password, host, port):
    """ sets up the proxy connection to the Account Server """
    return xmlrpclib.ServerProxy("http://" + user + ":" + password +
            "@" + host + ":" + port + "/xmlrpc");


if __name__ == '__main__':
    parser = OptionParser("%prog [options]", version="%prog " + VERSION)
    parser.add_option("-u", "--user", dest="user",
            help="username to login with")
    parser.add_option("-c", "--host", dest="host",
            help="hostname to connect to", default="accounts")
    parser.add_option("-p", "--port", dest="port",
            help="port to connect to", default="8081")

    (options, args) = parser.parse_args()

    if not options.user:
        parser.error("must specify a user")

    password = getpass()
    proxy = make_connection(options.user, password, options.host, options.port)

    print "user,IP,machine,id,date,enabled"
    for line in sys.stdin:
        check_user(proxy, line[:-1])
