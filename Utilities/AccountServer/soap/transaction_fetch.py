import csv
import re
import sys

from vindicia import vindicia_soap_client, vindicia_auth

login = "cryptic_soap"

host = sys.argv[1]
vin_host = sys.argv[2]
pw_path = sys.argv[3]

with open(pw_path, 'r') as f:
    exp = re.compile("^\s*vindiciaPassword\s*(\w+)")
    for line in f:
        result = exp.match(line)
        if result:
            password = result.group(1)
            break

vin_client = vindicia_soap_client("Transaction", host, vin_host)
vin_auth = vindicia_auth(vin_client, login, password)

page = 0
page_size = 50

w = csv.writer(sys.stdout)
w.writerow(('merchantAccountId', 'merchantTransactionId', 'timestamp', 'sourcePaymentMethodVID', 'paymentProcessor', 'status', 'type', 'authCode', 'currency', 'sku', 'price', 'autobillVID'))
while True:
    sys.stderr.write('Retrieving page %d\r' % page)
    result = vin_client.service.fetchDeltaSince(vin_auth, "2012-08-01T00:00:00", None, page, page_size, None)
    assert result["return"].returnCode == "200"

    for trans in result.transactions:
        line = []
        line += [trans.account.merchantAccountId, trans.merchantTransactionId, trans.timestamp]
        line += [trans.sourcePaymentMethod.VID, trans.paymentProcessor]
        line += [trans.statusLog[0].status, trans.statusLog[0].paymentMethodType]
        if trans.statusLog[0].paymentMethodType == "CreditCard":
            line += [trans.statusLog[0].creditCardStatus.authCode]
        else:
            line += [""]
        line += [trans.currency, trans.transactionItems[0].sku, trans.transactionItems[0].price]
        if "nameValues" in trans:
            for pair in trans.nameValues:
                if pair.name == "vin:AutoBillVID":
                    line += [pair.value]
                    break
            else:
                line += [""]
        w.writerow(line)

    if len(result.transactions) < page_size:
        break
    page += 1
    sys.stdout.flush()

sys.stderr.write('Complete!                   \n')
