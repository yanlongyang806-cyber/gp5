@echo on
cd c:\xampp\

echo "STOP" >> C:\buildstatus\php\stopdaemons.txt

sleep 10

start cmd.exe /C C:\xampp\mysql_stop.bat

sleep 2

start cmd.exe /C C:\xampp\apache_stop.bat

sleep 2
