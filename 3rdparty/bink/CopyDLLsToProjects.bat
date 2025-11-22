@echo off
if EXIST ..\..\..\..\FightClub\LABEL SET SRC=..\..\..\..
if EXIST ..\..\..\FightClub\LABEL SET SRC=..\..\..
if EXIST ..\..\FightClub\LABEL SET SRC=..\..
if EXIST ..\FightClub\LABEL SET SRC=..
echo SRC is %SRC%
for /F "eol=; tokens=1" %%a in (%SRC%\ProjectList.txt) do (
	for %%b in (*.DLL, *.pdb) do (
		xcopy %%b %SRC%\%%a\bin\ /Y
		@if ERRORLEVEL 1 goto error
	)
)

@echo Success!
@pause

goto end

:error

@echo.
@echo There was an error!
@echo.
@pause

goto end

:end

