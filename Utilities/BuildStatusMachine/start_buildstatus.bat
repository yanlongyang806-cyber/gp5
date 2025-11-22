@echo on
cd c:\xampp\


rm -f C:\buildstatus\php\stopdaemons.txt

sleep 2

start cmd.exe /C C:\xampp\apache_start.bat

sleep 2

start cmd.exe /C C:\xampp\mysql_start.bat

sleep 2

start cmd.exe /C C:\BuildStatus\run_builderstatus_daemon.bat

sleep 2

start cmd.exe /C C:\BuildStatus\run_email_daemon.bat

