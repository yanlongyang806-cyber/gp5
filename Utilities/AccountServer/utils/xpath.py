# For help, type "python xpath.py --help"

from optparse import OptionParser
import urllib
import xml.etree.ElementTree as ET
import sys
import csv
import getpass
from threaded_iter import threaded_iter


def resolve_xpath(host, xpath):
    """ Resolves an xpath into a value """
    try:
        # For now, parse XML--Alex is adding JSON support
        url = ("http://" + host + "/viewxpath?xpath=" + xpath + "&format=xml")
        xml = urllib.urlopen(url).read()
        xml = ET.fromstring(xml)
        elem_name = xpath.split('.')[-1].lower()
        idx = 0
        if '[' in elem_name: # XML currently returns the parent struct
            (elem_name, idx) = elem_name.split('[')
            idx = int(idx[:-1])
            xml = xml.find(elem_name)
        return xml.findall(elem_name)[idx].text
    except (IndexError, ET.ParseError):
        return None


def iter_xpaths(host, xpaths, *args):
    """ Takes an array of xpaths and a single set of args to pass to each """
    for xpath in xpaths:
        yield (xpath, resolve_xpath(host, xpath.format(*args)))


def iter_xpaths_multi(host, xpaths, args, num_threads=1):
    """ Iterate over an array of xpaths with an array of args """
    arg_len = len(args[0])
    def iter_func(*args):
        results = []
        for result in iter_xpaths(host, xpaths, *args):
            results.append(result)
        return (args, results)
    return threaded_iter(iter_func, args, num_threads)


if __name__ == '__main__':
    in_file = sys.stdin
    out_file = sys.stdout
    parser = OptionParser("usage: %prog [options] host xpaths")
    parser.add_option('-t', '--threads', dest='threads',
            default=1, type='int', help='number of threads [default: %default]')
    parser.add_option('-u', '--user', dest='user',
            default=None, type='str', help='auth with username (will prompt for password)')
    (options, args) = parser.parse_args()
    if len(args) < 2:
        parser.error("incorrect number of arguments")

    host = args[0]
    if options.user:
        password = getpass.getpass('Password:')
        host = '%s:%s@%s' % (options.user, password, host)

    script_args = []
    argreader = csv.reader(in_file)
    for row in argreader:
        script_args.append(tuple(row))

    writer = csv.writer(out_file)
    writer.writerow(['arg'] * len(script_args[0]) + args[1:])
    for cur_args, results in iter_xpaths_multi(
            host=host,
            xpaths=args[1:],
            args=script_args,
            num_threads=options.threads):
        row_elems = list(cur_args) + [x[1] for x in results]
        writer.writerow(row_elems)
