copy/y c:\src\utilities\SentryBin\SentryServer.exe n:\software\SentryBin
copy/y c:\src\utilities\SentryBin\SentryServer.pdb n:\software\SentryBin
CrypticSymStore \\somnus\data\symserv\dataCryptic add c:\src\utilities\SentryBin\SentryServer.exe
CrypticSymStore \\somnus\data\symserv\dataCryptic add c:\src\utilities\SentryBin\SentryServer.pdb

copy/y c:\src\utilities\SentryBin\Sentry.exe n:\software\SentryBin\Sentry.bin
copy/y c:\src\utilities\SentryBin\Sentry.pdb n:\software\SentryBin
CrypticSymStore \\somnus\data\symserv\dataCryptic add /f c:\src\utilities\SentryBin\Sentry.exe
CrypticSymStore \\somnus\data\symserv\dataCryptic add c:\src\utilities\SentryBin\Sentry.pdb

copy/y "c:\src\utilities\SentryBin\run SentryServer from N.bat" n:\software\SentryBin
