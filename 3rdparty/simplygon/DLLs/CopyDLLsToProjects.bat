@echo off
del nxcharacter*
if EXIST ..\..\..\..\FightClub\LABEL SET SRC=..\..\..\..
if EXIST ..\..\..\FightClub\LABEL SET SRC=..\..\..
if EXIST ..\..\FightClub\LABEL SET SRC=..\..
if EXIST ..\FightClub\LABEL SET SRC=..
echo SRC is %SRC%

REM if more projects are licensed to make use of simplygon, add them to the list contained in the outer for loop as well as to SVN after this file has been run.

for %%a in (Night) do (
	for %%b in (*.DLL, *.pdb, *.dat) do (
		xcopy %%b %SRC%\%%a\bin\ /Y /D
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

