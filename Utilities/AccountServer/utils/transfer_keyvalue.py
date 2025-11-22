from xmlrpclib import ServerProxy
from optparse import OptionParser


def load_account_list(filename):
    l = open(filename).readlines()
    return [i.strip() for i in l]


def get_proxy(host):
    return ServerProxy('http://%s:8081/xmlrpc' % host)


class InvalidUserException(Exception):
    pass


class NotSetException(Exception):
    pass


def get_keyvalue_list(account, proxy):
    response = proxy.GetKeyValueList(account)
    if response['Result'] != 'user_exists':
        raise InvalidUserException() 
    return response['List']


def get_keyvalue(kv_list, key):
    for pair in kv_list:
        if pair['Key'] == key:
            return int(pair['Value'])
    return 0


def set_keyvalue(account, key, value, proxy, reason):
    response = proxy.SetKeyValueEx({
        'AccountName': account,
        'Key': key,
        'Value': str(value),
        'Increment': 0,
        'Reason': reason,
        'Type': 0,
    })
    if response['Result'] != 'key_set':
        raise NotSetException()


def transfer_keyvalue(account, from_key, to_key, host, reason):
    proxy = get_proxy(host)
    kv_list = get_keyvalue_list(account, proxy)
    to_value = get_keyvalue(kv_list, from_key)
    from_value = get_keyvalue(kv_list, to_key)
    if from_value != to_value:
        set_keyvalue(account, to_key, to_value, proxy, reason)
    return (from_value, to_value)


def transfer_values(from_key, to_key, account_list_filename, host, reason):
    assert(from_key != to_key)
    account_list = load_account_list(account_list_filename)
    for account in account_list:
        try:
            (from_value, to_value) = transfer_keyvalue(account, from_key, to_key, host, reason)
            print 'Fixed: %s (from %d to %d)' % (account, from_value, to_value)
        except InvalidUserException:
            print 'Error: invalid user: %s' % account
        except NotSetException:
            print 'Error: could not set: %s' % account


if __name__ == "__main__":
    parser = OptionParser('usage: %prog from_key to_key account_list host reason')
    (options, args) = parser.parse_args()
    if len(args) != 5:
        parser.error('incorrect number of arguments')
    else:
        transfer_values(args[0], args[1], args[2], args[3], args[4])
