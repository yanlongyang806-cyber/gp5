import urllib2
import uuid

try:
    import json
except ImportError:
    import simplejson as json


class ServiceProxy(object):
    def __init__(self, service_url, service_name=None, version='1.0', **extra):
        self._version = str(version)
        self._service_url = service_url
        self._service_name = service_name
        self._extra = extra

    def __getattr__(self, name):
        if self._service_name != None:
            name = '%s.%s' % (self._service_name, name)
        return self.__class__(self._service_url, name, self._version, **self._extra)

    def __repr__(self):
        return '<%s %s version=%s>'%(self.__class__.name, self._service_name, self._version)

    def __call__(self, *args, **kwargs):
        headers = {'Content-Type': 'application/json-rpc'}
        if 'header_cb' in self._extra:
            headers.update(self._extra['header_cb']())
        headers.update(kwargs.pop('headers', {}))
        params = kwargs if len(kwargs) else args
        if kwargs and self._version != '2.0':
            raise Exception('Unsupported arg type for JSON-RPC 1.0 '
                                         '(the default version for this client, '
                                         'pass version="2.0" to use keyword arguments)')
        r = self._request(json.dumps({'jsonrpc': self._version,
                               'method': self._service_name,
                               'params': params,
                               'id': str(uuid.uuid1())}), headers)
        return json.loads(r)

    def _request(self, data, headers): # pragma: no cover
        req = urllib2.Request(self._service_url, data, headers)
        try:
            return urllib2.urlopen(req).read()
        except urllib2.HTTPError, e:
            # Catch both application/json-rpc and application/json
            if e.headers.get('Content-Type', '').startswith('application/json'):
                return e.read()
            else:
                raise
