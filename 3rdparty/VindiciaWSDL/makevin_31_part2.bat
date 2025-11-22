@echo off
set VINDICIAVERSION=3.1
.\soapcpp2 -I.\import -c -d%VINDICIAVERSION% -pVindiciaStructs -t .\%VINDICIAVERSION%\vindiciaStructsTemp.h

