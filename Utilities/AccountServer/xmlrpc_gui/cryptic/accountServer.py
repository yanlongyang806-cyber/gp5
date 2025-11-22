import xmlrpc
import time

def pollTransView(accountServer, id):
    response = accountServer.TransView(id)
    while response['Status'] == 'PROCESS':
        time.sleep(3)
        response = accountServer.TransView(id)
    return response
