@echo on
mkdir C:\BuildStatus

echo Getting latest buildstatus scripts
cd c:\src\utilities\BuildStatus
svn update c:\src\utilities\BuildStatus
xcopy /S C:\src\utilties\BuildStatus C:\BuildStatus

echo Installing xampp
start /wait N:\Software\xampp\xampp-win32-1.7.0.exe

echo Copying apache config
xcopy /Y C:\src\utilities\BuildStatus\apache\httpd.conf C:\xampp\apache\conf\

echo INFO: Copying link to starting buildstatusmachine to StartMenu\Programs\Startup\
set startup=C:\Documents and Settings\All Users\Start Menu\Programs\Startup\
copy /y "C:\src\utilties\BuildStatus\setup\start_buildstatus.lnk" "%startup%"

echo INFO: Copying link to desktop
set desktop=C:\Documents and Settings\All Users\Desktop\
copy /y "C:\src\utilties\BuildStatus\setup\start_buildstatus.lnk" "%desktop%"

echo INFO: Importing Auto Login for user cryptic into registry
regedit /s C:\Core\tools\programmers\ContinuousBuilder\Configs\common_configs\winlogon.reg

echo MANUAL STEP: share C:\buildstatus\

echo ALL DONE, start the system by running C:\BuildStatus\start_buildstatus.bat
pause 