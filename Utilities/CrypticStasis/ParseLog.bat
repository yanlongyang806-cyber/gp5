@echo off
if /%1==/ goto noargs

echo Parsing %1 ...
echo.

c:
cd %~dp0
mkdir output
mkdir output\extras
copy extras\*.* output\extras
c:\cryptic\tools\perl\bin\perl -Istasis\lib stasis\stasis add -version Cryptic -overall -popoverall -dir output -file %1

echo.
echo Complete. Please check %~dp0\output for results.
goto end

:noargs
echo Please create a shortcut to this batch file and drag NNO combat logs onto it. 

:end
echo.
pause

