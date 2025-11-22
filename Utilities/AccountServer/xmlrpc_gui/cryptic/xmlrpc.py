import xmlrpc.client
import sys

def getServerHandle(host, port):
    return xmlrpc.client.ServerProxy("http://" + host + ":" + str(port) + "/xmlrpc")

def humanResults(result, tabs = 0):
    output = ""
 
    for k, v in result.items():
        if (type(v) == dict):
            output += "\t" * tabs + k + " {\n"
            output += humanResults(v, tabs + 1)
            output += "\t" * tabs + "}\n"
        elif (type(v) == list):
            first = 1
            if type(v[0]) != dict:
                output += "\t" * tabs + k + " = "
            for x in v:
                if type(x) == dict:
                    output += "\t" * tabs + k + " {\n"
                    output += humanResults(x, tabs + 1)
                    output += "\t" * tabs + "}\n"
                else:
                    if first != 1:
                        output += ", "

                    output += repr(x)
                    first = 0

            if type(v[0]) != dict:
                output += "\n"
        else:
            output += "\t" * tabs
            output += k + " = " + repr(v) + "\n"
    return output
