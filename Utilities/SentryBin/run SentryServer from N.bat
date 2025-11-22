killall sentry.exe
killall sentryserver.exe

copy/y n:\software\SentryBin\SentryServer.exe  .
copy/y n:\software\SentryBin\SentryServer.pdb .

copy/y n:\software\SentryBin\Sentry.bin Sentry.exe
copy/y n:\software\SentryBin\Sentry.pdb .

start sentry.exe
SentryServer.exe -BeginStatusReporting SentryServer newfcdev 80

