from vindicia import vindicia_soap_client, vindicia_auth
import pprint


#import logging
#logging.basicConfig(level=logging.INFO)
#logging.getLogger('suds.client').setLevel(logging.DEBUG)

host = "localhost:4430"
vin_host = "soap.prodtest.sj.vindicia.com"
login = "cryptic_soap"
password = "I4FCJKbhbAp3EXHRqM0lkeo44t4DlwFB"
abl_vid = "30e6285f0298b0e8ed1ae60d3a38df583054f76a"
days = 30



vin_client = vindicia_soap_client("AutoBill", host, vin_host)
vin_auth = vindicia_auth(vin_client, login, password)

vin_abl = vin_client.factory.create('vin:AutoBill')
vin_abl.VID = abl_vid

result = vin_client.service.delayBillingByDays(vin_auth, vin_abl, days, False)
pprint.pprint(result)

#guid = ''
#vin_client = vindicia_soap_client("Account", host, vin_host)
#vin_auth = vindicia_auth(vin_client, login, password)
#result = vin_client.service.fetchByMerchantAccountId(vin_auth, 'QA' + guid)
#pprint.pprint(result)
