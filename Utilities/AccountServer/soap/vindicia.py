import suds.client
from suds.transport.http import HttpTransport
import socket
import urllib2
import httplib


# If you update this, you also need to change add_vindicia_namespaces
VINDICIA_API_VERSION = '3.7'


def add_vindicia_namespaces(client):
    """
    Makes doing a SOAP call against Vindicia a lot nicer by making it
    so that you can reference a three-letter code instead of an entire
    URL for object namespaces.
    """
    client.add_prefix("vin", "http://soap.vindicia.com/v3_7/Vindicia")
    client.add_prefix("abl", "http://soap.vindicia.com/v3_7/AutoBill")
    client.add_prefix("acc", "http://soap.vindicia.com/v3_7/Account")
    client.add_prefix("act", "http://soap.vindicia.com/v3_7/Activity")
    client.add_prefix("add", "http://soap.vindicia.com/v3_7/Address")
    client.add_prefix("bpl", "http://soap.vindicia.com/v3_7/BillingPlan")
    client.add_prefix("cgb", "http://soap.vindicia.com/v3_7/Chargeback")
    client.add_prefix("ecs", "http://soap.vindicia.com/v3_7/ElectronicSignature")
    client.add_prefix("ent", "http://soap.vindicia.com/v3_7/Entitlement")
    client.add_prefix("etp", "http://soap.vindicia.com/v3_7/EmailTemplate")
    client.add_prefix("met", "http://soap.vindicia.com/v3_7/MetricStatistics")
    client.add_prefix("prd", "http://soap.vindicia.com/v3_7/Product")
    client.add_prefix("pym", "http://soap.vindicia.com/v3_7/PaymentMethod")
    client.add_prefix("pyp", "http://soap.vindicia.com/v3_7/PaymentProvider")
    client.add_prefix("rfd", "http://soap.vindicia.com/v3_7/Refund")
    client.add_prefix("trn", "http://soap.vindicia.com/v3_7/Transaction")


class RedirectedHTTPSHandler(urllib2.HTTPSHandler):
    """ Used by the below class; for stunnel connections """

    def __init__(self, host):
        urllib2.HTTPSHandler.__init__(self)
        self.host = host

    def https_open(self, req):
        return self.do_open(self.get_connection, req)

    def get_connection(self, host, timeout=300):
        return httplib.HTTPConnection(self.host)


class RedirectedTransport(HttpTransport):
    """ To make stunnel-based connections work """

    def __init__(self, host, *args, **kwargs):
        HttpTransport.__init__(self, *args, **kwargs)
        self.host = host

    def u2open(self, u2request):
        opener = urllib2.build_opener(RedirectedHTTPSHandler(self.host))
        return opener.open(u2request, timeout=self.options.timeout)


def vindicia_soap_client(object, host, vin_host):
    """
    Creates a new client connection to Vindicia; takes a Vindicia object
    name (like "Account").
    """
    client = suds.client.Client("https://%s/%s/%s.wsdl" % (vin_host, VINDICIA_API_VERSION, object),
            transport=RedirectedTransport(host))
    add_vindicia_namespaces(client)
    return client


def vindicia_auth(vin_client, login, password):
    """ Fills in the auth portion of a Vindicia request """
    auth = vin_client.factory.create('vin:Authentication')
    auth.login = login
    auth.password = password
    auth.version = VINDICIA_API_VERSION
    return auth
