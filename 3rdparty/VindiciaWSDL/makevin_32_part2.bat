@echo off
set VINDICIAVERSION=3.2
.\soapcpp2 -I.\import -c -d%VINDICIAVERSION% -pVindiciaStructs -t .\%VINDICIAVERSION%\vindiciaStructsTemp.h

