import re
import xmlrpclib

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

vin_client = vindicia_soap_client("PaymentMethod", host, vin_host)
vin_auth = vindicia_auth(vin_client, login, password)

vin_pm = vin_client.factory.create('vin:PaymentMethod')
vin_pm.VID = 'fc03898b7929cbbfe8c815e86b9e1e464a423ed9'

vin_client.service.update(
