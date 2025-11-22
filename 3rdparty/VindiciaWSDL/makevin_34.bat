@echo off

set VINDICIAVERSION=3.4
set VINDICIAURL=http://soap.staging.sj.vindicia.com/

mkdir %VINDICIAVERSION%

.\wsdl2h -t.\typemap.dat -c %VINDICIAURL%%VINDICIAVERSION%/Account.wsdl %VINDICIAURL%%VINDICIAVERSION%/Activity.wsdl %VINDICIAURL%%VINDICIAVERSION%/Address.wsdl %VINDICIAURL%%VINDICIAVERSION%/AutoBill.wsdl %VINDICIAURL%%VINDICIAVERSION%/BillingPlan.wsdl %VINDICIAURL%%VINDICIAVERSION%/Chargeback.wsdl %VINDICIAURL%%VINDICIAVERSION%/ElectronicSignature.wsdl %VINDICIAURL%%VINDICIAVERSION%/EmailTemplate.wsdl %VINDICIAURL%%VINDICIAVERSION%/Entitlement.wsdl %VINDICIAURL%%VINDICIAVERSION%/MetricStatistics.wsdl %VINDICIAURL%%VINDICIAVERSION%/PaymentMethod.wsdl %VINDICIAURL%%VINDICIAVERSION%/PaymentProvider.wsdl %VINDICIAURL%%VINDICIAVERSION%/Product.wsdl %VINDICIAURL%%VINDICIAVERSION%/Refund.wsdl %VINDICIAURL%%VINDICIAVERSION%/Transaction.wsdl > .\%VINDICIAVERSION%\vindiciaStructsTemp.h
.\soapcpp2 -I.\import -c -d%VINDICIAVERSION% -pVindiciaStructs -t .\%VINDICIAVERSION%\vindiciaStructsTemp.h

del .\%VINDICIAVERSION%\*.xml
del .\%VINDICIAVERSION%\*.nsmap
del .\%VINDICIAVERSION%\VindiciaStructsClientLib.c
del .\%VINDICIAVERSION%\VindiciaStructsServer.c
del .\%VINDICIAVERSION%\VindiciaStructsServerLib.c
del .\%VINDICIAVERSION%\vindiciaStructsTemp.h