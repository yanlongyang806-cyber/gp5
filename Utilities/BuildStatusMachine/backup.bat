call C:\BuildStatus\stop_buildstatus.bat

xcopy /S /Y C:\BuildStatus \\vesta\cb-master\buildstatus\

xcopy /S /Y C:\xampp\mysql\data \\vesta\cb-master\mysql\

sleep 2

call C:\BuildStatus\start_buildstatus.bat

