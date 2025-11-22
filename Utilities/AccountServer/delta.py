from optparse import OptionParser
from bisect import bisect_left
import os
import re
import time
import httplib
import urllib
from base64 import b64encode
import datetime
import sys
import time
import logging
import logging.handlers
import ConfigParser

config = ConfigParser.SafeConfigParser({
        'Host': '172.20.3.28',
        'Sleep': '6',
        'RefreshURL': '/legacy/refreshSubscriptionCache?',
        'DetailsURL': '/legacy/detail?',
        'Username': '',
        'Password': '',
        'LogFilename': 'delta.log',
    })
config.read('delta.cfg')

class AccountNameError(Exception):
    def __init__(self, value):
        self.value = value

    def __str__(self):
        return self.value


class GUIDError(Exception):
    def __init__(self, value):
        self.value = value

    def __str__(self):
        return self.value


class RefreshError(Exception):
    def __init__(self, value):
        self.value = value

    def __str__(self):
        return self.value


def in_sorted_list(list, item):
    insert_point = bisect_left(list, item)
    if insert_point == len(list):
        return False
    return list[insert_point] == item


def add_unique_to_sorted_list(list, item):
    insert_point = bisect_left(list, item)
    if insert_point == len(list) or list[insert_point] != item:
        list.insert(insert_point, item)


def get_guid(line):
    matches = re.match(r'"(?P<AutoBillName>.*?)","(?P<Product>.*?)","(?P<MerchantAutoBillID>.*?)","(?P<CustomerName>.*?)","(?P<CustomerEmail>.*?)","(?P<CustomerID>.*?)","(?P<BillingPlan>.*?)","(?P<StartDate>.*?)","(?P<EndDate>.*?)","(?P<BillingDay>.*?)","(?P<Status>.*?)","(?P<Entitled>.*?)","(?P<CreatedDate>.*?)"', line)
    if not matches:
        raise GUIDError('Could not get GUID from CSV line.')
    return matches.group('CustomerID')


def get_page(page):
    headers = {
            'Host': config.get('DEFAULT', 'Host'),
            'Authorization': 'Basic ' + b64encode(config.get('DEFAULT', 'Username') + ':' + config.get('DEFAULT', 'Password')),
            }
    conn = httplib.HTTPConnection(config.get('DEFAULT', 'Host'))
    conn.request('GET', page, None, headers)
    response = conn.getresponse()
    data = response.read()
    result = response.status
    conn.close()
    return (data, result)


def get_account_name(guid):
    url = config.get('DEFAULT', 'DetailsURL') + urllib.urlencode({'guid': guid})
    (data, result) = get_page(url)
    match = re.search(r'name="accountname" value="(?P<name>.*?)"', data)
    if not match:
        raise AccountNameError('Could not get account name for GUID ' + guid)
    return match.group('name')


def refresh_account(account):
    url = config.get('DEFAULT', 'RefreshURL') + urllib.urlencode({'name': account})
    (data, result) = get_page(url)
    if result != 302:
        raise RefreshError('Refresh did not result in 302 status code.')


def format_modified_time(file):
    t = os.path.getmtime(file)
    t = time.gmtime(t)
    return time.strftime('%Y-%m-%d %H:%M:%S', t)


def main():
    parser = OptionParser(usage="usage: %prog oldfile newfile [-f]", version="%prog 1.0")
    parser.add_option('-f', dest="force", action="store_true", help='Force execution even if oldfile appears newer than newfile.')
    (options, args) = parser.parse_args()
    if len(args) != 2:
        parser.error("incorrect number of arguments")
    if os.path.getmtime(args[0]) > os.path.getmtime(args[1]):
        if not options.force:
            parser.error('oldfile appears newer than newfile! Use -f to ignore this warning.')

    logger = logging.getLogger('DEFAULT')
    logger.setLevel(logging.DEBUG)

    file_handler = logging.handlers.RotatingFileHandler(config.get('DEFAULT', 'LogFilename'))
    stream_handler = logging.StreamHandler()

    formatter = logging.Formatter("%(asctime)s - %(levelname)s: %(message)s")
    file_handler.setFormatter(formatter)

    logger.addHandler(file_handler)
    logger.addHandler(stream_handler)

    logger.info('Starting Delta.')

    try:
        logger.info('Loading old file ' + args[0] + ' (last modified ' + format_modified_time(args[0]) + ') ...')
        old_lines = sorted(open(args[0]).readlines())

        logger.info('Loading new file ' + args[1] + ' (last modified ' + format_modified_time(args[1]) + ') ...')
        guids = []
        new_file = open(args[1])
        for line in new_file:
            if not in_sorted_list(old_lines, line):
                guid = get_guid(line)
                add_unique_to_sorted_list(guids, guid)

        logger.info(str(len(guids)) + ' entries.')

        logger.info('Refreshing...')
        current = float(1)
        for guid in guids:
            seconds = (len(guids) - current) * config.getfloat('DEFAULT', 'Sleep')
            percent = current / len(guids) * 100
            current = current + 1

            start_time = time.time()

            message = '%.2f%% (~%s remaining) %s ' % (percent, str(datetime.timedelta(seconds=seconds)), guid)
            try:
                account_name = get_account_name(guid)
                refresh_account(account_name)

                message = message + account_name
            except AccountNameError, error:
                logger.error(error)
                message = message + '(unknown) Error: could not determine account name!'
            except RefreshError, error:
                logger.error(error)
                message = message + account_name + ' Error: could not refresh!'
            
            elapsed_time = time.time() - start_time
            message = message + ' (%.2fs)' % (elapsed_time)

            logger.info(message)

            time_to_sleep = config.getfloat('DEFAULT', 'Sleep') - elapsed_time
            if time_to_sleep < 1:
                time_to_sleep = 1
            time.sleep(time_to_sleep)

        logger.info('Delta finished.')

    except IOError, error:
        logger.critical(error)
    except GUIDError, error:
        logger.critical(error)
    except KeyboardInterrupt:
        logger.warning('Script terminated.')


main()
