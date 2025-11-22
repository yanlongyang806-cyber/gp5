import fileinput
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

vin_client = vindicia_soap_client("AutoBill", host, vin_host)
vin_auth = vindicia_auth(vin_client, login, password)
vin_abl = vin_client.factory.create("vin:AutoBill")

delay_date = "2012-08-13T00:00:00"

for line in fileinput.input(sys.argv[4:]):
    vin_abl.VID = line.strip()
    result = vin_client.service.delayBillingToDate(vin_auth, vin_abl, delay_date, False)
    if result["return"].returnCode != "200":
        print result
