@echo off

set VINDICIAVERSION=3.1

mkdir %VINDICIAVERSION%

.\wsdl2h -t.\typemap.dat -c http://soap.vindicia.com/%VINDICIAVERSION%/Account.wsdl http://soap.vindicia.com/%VINDICIAVERSION%/Activity.wsdl http://soap.vindicia.com/%VINDICIAVERSION%/Address.wsdl http://soap.vindicia.com/%VINDICIAVERSION%/AutoBill.wsdl http://soap.vindicia.com/%VINDICIAVERSION%/BillingPlan.wsdl http://soap.vindicia.com/%VINDICIAVERSION%/Chargeback.wsdl http://soap.vindicia.com/%VINDICIAVERSION%/ElectronicSignature.wsdl http://soap.vindicia.com/%VINDICIAVERSION%/EmailTemplate.wsdl http://soap.vindicia.com/%VINDICIAVERSION%/Entitlement.wsdl http://soap.vindicia.com/%VINDICIAVERSION%/MetricStatistics.wsdl http://soap.vindicia.com/%VINDICIAVERSION%/PaymentMethod.wsdl http://soap.vindicia.com/%VINDICIAVERSION%/PaymentProvider.wsdl http://soap.vindicia.com/%VINDICIAVERSION%/Product.wsdl http://soap.vindicia.com/%VINDICIAVERSION%/Refund.wsdl http://soap.vindicia.com/%VINDICIAVERSION%/Transaction.wsdl > .\%VINDICIAVERSION%\vindiciaStructsTemp.h

