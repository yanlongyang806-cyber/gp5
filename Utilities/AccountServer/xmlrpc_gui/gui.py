from tkinter import *
from tkinter.constants import *
from tkinter.tix import *
from tkinter.ttk import *
import cryptic.xmlrpc
import cryptic.accountServer
import sys
import http.client
import configparser
import socket
import os
import time
import pprint
import pickle
import hashlib

config = configparser.SafeConfigParser()
config.read(['accountServer.cfg.template', 'accountServer.cfg'])

host = config.get('Connection', 'host')
port = config.get('Connection', 'port')

accountServer = cryptic.xmlrpc.getServerHandle(host, port)

root = tkinter.tix.Tk()

def csv(x):
    if len(x) > 0:
        return x.split(',')
    return 'NULL'

def csvint(x):
    if len(x) > 0:
        values = x.split(',')
        values = list(map(lambda x: int(x), values))
        return values
    return 'NULL'

def sha256(x):
    h = hashlib.new('sha256')
    h.update(x.encode('UTF-8'))
    return h.hexdigest()

paramstruct = 'Paramstruct'

paymentMethod = {
    'VID': 'NULL',
    'Active': '1',
    ('Account Holder Name', 'AccountHolderName'): 'Joe Smith',
    ('Customer Specified Type', 'CustomerSpecifiedType'): 'Visa',
    ('Customer Description', 'CustomerDescription'): 'Credit card',
    'Currency': 'EUR',
    ('Address Name', 'AddressName'): 'Home',
    ('Address Line 1', 'Addr1'): '980 University Avenue',
    ('Address Line 2', 'Addr2'): 'GameOps',
    'City': 'Los Gatos',
    'County': 'Santa Clara',
    'District': 'NULL',
    ('Postal Code', 'PostalCode'): '95032',
    'Country': 'DE',
    'Phone': '408-399-1969',
    'precreated': '1',
    'PayPal':
        {
            ('E-mail Address', 'EmailAddress'): 'NULL',
	        ('Return URL', 'ReturnUrl'): 'NULL',
	        ('Cancel URL', 'CancelUrl'): 'NULL',
	        'Password': 'NULL',
        },
    ('Credit Card', 'CreditCard'):
        {
            'CVV2': '123',
	        'Account': '4485983356242217',
            ('Expiration Date', 'ExpirationDate'): '201202',
        }, 
    ('Direct Debit', 'DirectDebit'):
        {
	    'Account': 'NULL',#'1234567890',
	    'BankSortCode': 'NULL',#'12345678',
	    'RibCode': 'NULL',
	},
}

methods = {
    'ValidateLoginEx':
        [
            (paramstruct,
                {
                    'accountName': 'cogden',
                    ('password', 'sha256Password'): (sha256, ''),
                    'crypticPasswordWithAccountNameAndNewStyleSalt': '',
                    'md5Password': '',
                    'flags': '0',
                    'ips': (csv, ''),
                    'salt': '0',
                    'location': '',
                    'referrer': '',
                    'clientVersion': '',
                    'note': ''
                })
        ],
    'BlockedIPs': [],
    'GetPurchaseLogEx':
        [
            (paramstruct,
                {
                    'uSinceSS2000': '0',
                    'uMaxResponses': '50',
                    'uAccountID': '0',
                })
        ],
    'Error': [],
    'SetLoginEnabled':
        [
            (paramstruct,
                {
                    'accountName': '',
                    'enabled': '1',
                })
        ],
    'PW::TransferCurrency':
        [
            (paramstruct,
                {
                    'AccountName': 'test',
                    'Currency': 'Test',
                    'Amount': '500'
                })
        ],
    'PW::UpdateAccount':
        [
            (paramstruct,
                {
                    'AccountName': 'test',
                    'ForumName': 'test',
                    'Email': 'test',
                    'PasswordHash': '0'
                })
        ],
    'LinkToPWAccount':
        [
            (paramstruct,
                {
                    'PWAccountName': 'test',
                    'CrypticAccountName': 'test',
                    'Flags': '0'
                })
        ],
    'SuperSubCreate':
        [
            (paramstruct,
                {
                    'User': 'cogden',
                    'Subscription': 'CO-Lifetime',
                    ('Payment Method', 'PaymentMethod'): paymentMethod,
                    'Currency': 'USD',
                    'IP': '127.0.0.1',
                    ('Activation Keys', 'ActivationKeys'): (csv, ''),
                    'Referrer': '',
                })
        ],
    'SubGrantDays':
        [
            ('User', 'cogden'),
            ('VID', ''),
            ('Days', '1'),
        ],
    'DoPendingAction':
        [
            (paramstruct,
                {
                    'Account': 'cogden',
                    ('Action ID', 'uActionID'): (int, '1'),
                })
        ],
    'TransView':
        [
            ('Trans ID', ''),
        ],
    'Stats': [],
    'ListSubscriptions': [],
    'UpdatePaymentMethod':
        [
            ('User', 'cogden'),
            ('PaymentMethod', paymentMethod),
            ('IP', '127.0.0.1'),
        ],
    'ChangePaymentMethod':
        [
            (paramstruct,
                {
                    'AccountName': 'cogden',
                    ('Payment Method', 'PaymentMethod'): paymentMethod,
                    'IP': '127.0.0.1',
                    ('Bank Name', 'bankName'): 'Nelson',
                }),
        ],
    'SubCancel':
        [
            (paramstruct,
                {
                    'User': 'cogden',
                    'VID': '',
                    'Instant': '0',
                    'MerchantInitiated': '0',
                })
        ],
    'ChangeKeyValue':
        [
            ('Account Name', 'cogden'),
            ('Key', ''),
            ('Value', ''),
        ],
    'SetKeyValue':
        [
            ('Account Name', 'cogden'),
            ('Key', ''),
            ('Value', ''),
        ],
    'SetKeyValueEx':
        [
            (paramstruct, {
                'AccountName': 'test',
                'Key': '',
                'Value': '',
                'Reason': '',
            })
        ],
    'SetOrChangeKeyValue':
        [
            ('Account Name', 'cogden'),
            ('Key', ''),
            ('Value', ''),
            ('Change', '1'),
        ],
    'GetKeyValueList':
        [
            ('Account Name', 'cogden'),
        ],
    'TakeProduct':
        [
            ('Account Name', 'cogden'),
            ('Product Name', ''),
        ],
    'GiveProduct':
        [
            ('Account Name', 'cogden'),
            ('Product Name', ''),
            ('Key', ''),
        ],
    'ActivateProduct':
        [
            (paramstruct,
                {
                    ('Account Name', 'AccountName'): 'cogden',
                    ('Product Name', 'ProductName'): '',
                    ('Key', 'ProductKey'): '',
                    ('Recruit Email', 'RecruitEmailAddress'): '',
                    'Referrer': '',
                }),
        ],
    'ListProducts': [],
    'NextUserInfo':
        [
            ('Account ID', '1'),
            ('Flags', '0'),
        ],
    'UserInfo':
        [
            ('Account Name', 'cogden'),
            ('Flags', '0'),
        ],
    'UserInfoEx':
        [
            ('Account Name', 'vsarpeshkar'),
            ('Flags', '0'),
            ('Type', '1'),
        ],
    'ValidateTicketID':
        [
            ('Ticket ID', ''),
            ('Account ID', ''),
            ('Flags', '0'),
        ],
    'ValidateTicket':
        [
            ('Ticket', ''),
        ],
    'IsValidProductKey':
        [
            ('Product Key', ''),
        ],
    'ActivateProductKey':
        [
            ('Account Name', 'cogden'),
            ('Product Key', ''),
        ],
    'DeleteAccount':
        [
            ('Account Name', 'tchao'),
        ],
    'ValidateAccountEmail':
        [
            ('Account Name', 'cogden'),
            ('Validate E-mail Key', ''),
            ('Send Permissions', '1'),
        ],
    'ResendEmailValidationKey':
        [
            ('Account Name', 'cogden'),
        ],
    'Purchase':
        [
            (paramstruct,
                {
                    'User': 'cogden',
                    'Currency': 'EUR',
                    ('Payment Method', 'PaymentMethod'): paymentMethod,
                    ('Product IDs', 'ProductID'): (csvint, '0'),
                    'IP': '127.0.0.1',
                    ('Bank Name', 'bankName'): 'Nelson',
                }),
        ],
    'PurchaseEx':
        [
            (paramstruct,
                {
                    'User': 'cogden',
                    'Currency': 'USD',
                    ('Payment Method', 'PaymentMethod'): paymentMethod,
                    ('Item', 'Items'):
                        [
                            (paramstruct,
                                {
                                    ('Product ID', 'ProductID'): '0',
                                    'Price': '',
                                }),
                        ],
                    'IP': '127.0.0.1',
                    ('Bank Name', 'bankName'): 'Nelson',
                    ('Auth Only?', 'AuthOnly'): '1',
                    ('Verify Price?', 'VerifyPrice'): '0',
                    ('Steam ID', 'Steamid'): '',
                    'Source': 'WebFC',
                    ('Locale Code', 'locCode'): 'EN',
                }),
        ],
    'CompletePurchase':
        [
            (paramstruct,
                {
                    'User': 'cogden',
                    ('Purchase ID', 'PurchaseID'): '',
                }),
        ],
    'GetTransactions':
        [
            ('Account Name', ''),
        ],
    'Refund':
        [
            (paramstruct,
                {
                    ('Account Name', 'AccountName'): 'cogden',
                    'Transaction': '',
                    ('Refund with Vindicia', 'RefundWithVindicia'): '0',
                    'Amount': '',
                    ('Sub VID', 'OptionalSubVID'): '',
                }),
        ],
    'MarkSubRefunded':
        [
            (paramstruct,
                {
                    ('Account Name', 'Account'): 'cogden',
                    ('Subscription VID', 'SubVID'): '',
                }),
        ],
    'ArchiveSubHistory':
        [
            (paramstruct,
                {
                    ('Account Name', 'AccountName'): 'cogden',
                    ('Product Internal Name', 'ProductInternalName'): 'FightClub',
                    ('Sub Internal Name', 'SubInternalName'): 'FightClub',
                    ('Sub VID', 'SubVID'): '',
                    ('Start Time', 'StartTime'): '',
                    ('End Time', 'EndTime'): '',
                    ('Sub Time Source', 'SubTimeSource'): 'External',
                    ('Problem Flags', 'ProblemFlags'): '0',
                }),
        ],
    'RecalculateArchivedSubHistory':
        [
            (paramstruct,
                {
                    ('Account Name', 'AccountName'): 'cogden',
                    ('Product Internal Name', 'ProductInternalName'): 'FightClub',
                }),
        ],
    'EnableArchivedSubHistory':
        [
            (paramstruct,
                {
                    ('Account Name', 'AccountName'): 'cogden',
                    ('Product Internal Name', 'ProductInternalName'): 'FightClub',
                    'ID': '1',
                    'Enable': '1',
                }),
        ],
    'ChangeSubCreatedTime':
        [
            (paramstruct,
                {
                    ('Account Name', 'AccountName'): 'cogden',
                    ('Sub VID', 'SubVID'): '',
                    ('New Created Time', 'NewCreatedTime'): '',
                }),
        ],
    'RecruitmentOffered':
        [
            (paramstruct, {
                    ('Account Name', 'AccountName'): 'cogden',
                    ('Product Key', 'ProductKey'): '',
                })
        ],
    'TransactionFetchDelta':
        [
            (paramstruct, {
                    ('Start', 'startSS2000'): '0',
                    ('End', 'endSS2000'): '0',
                    'filters': '0',
                })
        ],
    'ForbidPaymentMethod':
        [
            (paramstruct, {
                    'AccountName': 'cogden',
                    'ForbiddenTypes': '0',
                })
        ],
    'CreateNewAccount':
        [
            (paramstruct, {
                    'accountName': 'cogden',
                    'passwordHash': '1',
                    'displayName': 'cogden',
                    'email': 'cogden@crypticstudios.com',
                    'firstName': 'chris',
                    'lastName': 'awesome',
                    'ips': (csv, '127.0.0.1'),
                })
        ],
    'MarkRecruitBilled':
        [
            (paramstruct, {
                    'RecruiterAccountName': '',
                    'RecruitAccountName': '',
                    'ProductInternalName': '',
                })
        ],
    'SteamGetUserInfo':
        [
            (paramstruct, {
                ('Steam ID', 'Steamid'): '76561198044483429',
                'IP': '127.0.0.1',
                'Source': 'WebFC',
            }),
        ],
    'SteamRefund':
        [
            (paramstruct, {
                ('Order ID', 'Orderid'): '',
                'Source': 'WebFC',
                ('Account Name', 'Accountname'): 'cogden',
            }),
        ],
}

def stringVarize(something):
    if type(something) == str:
        return StringVar(value=something)
    if type(something) == tuple:
        return (something[0], StringVar(value=something[1]))
    if type(something) == int:
        return (int, StringVar(value=something))
    if type(something) == list:
        newList = []
        for item in something:
            newList.append((item[0], stringVarize(item[1])))
        return newList
    if type(something) == dict:
        newDict = {}
        for k, v in something.items():
            newDict[k] = stringVarize(v)
        return newDict

methods = stringVarize(methods)

def convertMethod(method):
    if type(method) == list:
        newList = []
        for k in method:
            newList.append(convertMethod(k[1]))
        return newList
    elif type(method) == dict:
        newMethod = {}
        for k, v in method.items():
            if type(k) == tuple:
                k = k[1]
            result = convertMethod(v)
            if type(result) == str:
                if result != 'NULL':
                    newMethod[k] = result
            else:
                newMethod[k] = convertMethod(v)
               
        if len(newMethod) > 0:
            return newMethod
        return 'NULL'

    elif type(method) == tuple:
        return method[0](convertMethod(method[1]))
    elif type(method) == int:
        return method
    else:
        return str(method.get())

def sortMethod(x):
    if type(x) == tuple:
        return x[0]
    return x

class Application(Frame):
    def createForm(self, parent, label, params):
        if type(label) == tuple:
            label = label[0]

        if type(params) == dict:

            if label != paramstruct:
                frame = LabelFrame(parent, text=label)
                parent = frame

            keys = list(params.keys());
            keys.sort(key=sortMethod)
            for k in keys:
                self.createForm(parent, k, params[k])

            if label != paramstruct:
                frame.pack({'fill': 'x', 'padx': 5, 'pady': 3})
        elif type(params) == list:
            if label != paramstruct:
                frame = LabelFrame(parent, text=label)
                parent = frame

            if len(params) == 0:
                label = Label(parent, text='No parameters required')
                label.pack({'fill': 'x', 'padx': 5, 'pady': 3})

            for k in params:
                if type(k) == tuple:
                    self.createForm(parent, k[0], k[1])
                else:
                    self.createForm(parent, label, k)

            if label != paramstruct:
                frame.pack({'fill': 'x', 'padx': 5, 'pady': 3})
        else:
            name = label
            value = params

            if type(params) == tuple:
                value = params[1]

            frame = Frame(parent)
            if type(value) == str:
                text = Entry(frame)
                text.insert(0, value)
                text.pack({'side': 'right'})
            else:
                text = Entry(frame, textvariable=value)
                text.pack({'side': 'right'})

            label = Label(frame, text=name + ':')
            label.pack({'side': 'right'})
            frame.pack({'fill': 'x', 'padx': 5, 'pady': 3})


    def populateMethod(self, methodName):
        method = methods[methodName]

        top = self.requestFrame
        top.configure(text='XML-RPC Request')

        self.clearRequestFrame()

        frameWindow = ScrolledWindow(top, scrollbar='auto', height=1)
        frameWindow.pack({'fill': 'both', 'expand': 1, 'padx': 5, 'pady': 3})

        self.createForm(frameWindow.window, methodName, method)

        frame = Frame(top)
        submitButton = Button(frame, text='Submit Request', command=lambda m=method, n=methodName: self.doMethod(m, n))
        submitButton.pack({'side': 'right', 'padx': 5, 'pady': 3})
        frame.pack({'side': 'bottom', 'fill': 'x'})

    def clearRequestFrame(self):
        for widget in list(self.requestFrame.children.values()):
            widget.destroy()

    def setCenterMessage(self, subject, message):
        self.requestFrame.configure(text=subject)
        self.clearRequestFrame()
        Label(self.requestFrame, text=message, justify='center', wraplength=300, anchor=CENTER).pack({'fill': 'both', 'expand': 1})

    def doMethod(self, method, methodName):
        self.selectedMethod.set(self.selectMethodText)
        top = self.requestFrame

        params = convertMethod(method)

        response = []

        try:
            xmlrpc = getattr(accountServer, methodName)
            response = xmlrpc(*params)
        except Exception as inst:
            self.setCenterMessage('XML-RPC Error', inst)
        else:
            if methodName == 'TransView' and response['Status'] == 'PROCESS':
                if top['text'] == 'XML-RPC Request' or top['text'] == 'XML-RPC Response':
                    self.setCenterMessage('XML-RPC TransView Wait', 'Please wait while TransView processes.')
                    bar = Progressbar(top, mode='indeterminate')
                    bar.pack({'fill': 'x', 'padx': 5, 'pady': 3})
                    bar.start()

                self.parent.after(2000, self.doMethod, method, methodName)

            else:
                top.configure(text='XML-RPC Response')
                self.clearRequestFrame()

                frameWindow = ScrolledWindow(top, scrollbar='auto', height=1)
                frameWindow.pack({'fill': 'both', 'expand': 1, 'padx': 5, 'pady': 3})

                self.createForm(frameWindow.window, methodName + ' Response', response)

                transid = ''
                for k, v in response.items():
                    if type(k) == str and k == 'Transid' and methodName != 'TransView':
                        transid = v

                if len(transid) > 0:
                    frame = Frame(top)
                    transButton = Button(frame, text='Do TransView', command=lambda m=methods['TransView']: self.doMethod(m, 'TransView'))
                    transButton.pack({'side': 'right', 'padx': 5, 'pady': 3})
                    methods['TransView'][0][1].set(transid)
                    frame.pack({'side': 'bottom', 'fill': 'x'})

    def createWidgets(self):
        self.methodFrame = LabelFrame(self, text='XML-RPC Methods')

        methodList = list(methods.keys())
        methodList.sort()

        self.selectMethodText = 'Please select a method to execute.'

        self.selectedMethod = StringVar(value=self.selectMethodText)

        option = OptionMenu(self.methodFrame, self.selectedMethod, self.selectMethodText, *methodList, command=self.populateMethod)
        option.pack({'fill': 'x', 'expand': 1, 'padx': 5, 'pady': 3})

        self.methodFrame.pack({'padx': 5, 'pady': 3, 'side': 'top', 'fill': 'x'})

        self.requestFrame = LabelFrame(self, text='XML-RPC Request')
        self.requestFrame.pack({'padx': 5, 'pady': 3, 'side': 'top', 'fill': 'both', 'expand': 1})

        self.setCenterMessage('XML-RPC Request', 'Please select an XML-RPC method in the above drop-down.')

    def __init__(self, master=None):
        Frame.__init__(self, master)
        master.minsize(400, 600)
        self.pack({'fill': 'both', 'expand': 1})
        self.createWidgets()
        master.title('XML-RPC Tester')
        self.parent = master


app = Application(master=root)
app.mainloop()
