@echo off
if EXIST ..\..\..\..\FightClub\LABEL SET SRC=..\..\..\..
if EXIST ..\..\..\FightClub\LABEL SET SRC=..\..\..
if EXIST ..\..\FightClub\LABEL SET SRC=..\..
if EXIST ..\FightClub\LABEL SET SRC=..
echo SRC is %SRC%
for %%a in (Night,StarTrek,Core) do (
        echo Copying CEF to %SRC%\%%a\bin
	xcopy bin\* %SRC%\%%a\bin /y /d /s
)

echo Success!
pause

goto end

:error

echo.
echo There was an error!
echo.
pause

goto end

:end

