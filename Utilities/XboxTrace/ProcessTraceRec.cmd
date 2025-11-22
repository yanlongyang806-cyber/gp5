rem @echo off
CompareStrings %1 ""
if %ERRORLEVEL% == -1 goto error

rem Use a named variable to make script more readable
set BINFILE=%1.bin

rem Make the base output file name
set OUTNAME=%1

rem Move the trace from the Xbox
xbcp xe:\%BINFILE% .
if not %errorlevel% == 0 goto copy_error

xbdel xe:\%BINFILE%

tracedump %BINFILE% "-capoutput=%OUTNAME% call profile.cap"
tracedump %BINFILE% -penaltycount=30 > "%OUTNAME% penalties.txt"
tracedump %BINFILE% -dumpmemaccessmap > "%OUTNAME% memory stats.txt"

goto end

:copy_error
goto end

:error
echo Usage: ProcessTraceRec.cmd INPUT_TRACE_BIN_FILE
echo Don't include the .bin extension in the INPUT_TRACE_BIN_FILE parameter.

:end
