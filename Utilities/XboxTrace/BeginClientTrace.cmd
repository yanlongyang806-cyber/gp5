@echo off
rem This script makes a specifically named copy of the client EXE
rem and PDB, to attach the CPU trace and call capture to specific
rem EXE and PDB names, and preserve the EXE and PDB for comparitive 
rem analysis.

echo Copying client EXE and PDB
copy GameClientXBOX.exe %1.exe
copy GameClientXBOX.pdb %1.pdb

echo Transferring EXE to Xbox
xbcp /Y %1.exe xe:\FCGameClient\

echo Launching the copied client
xbreboot xe:\FCGameClient\%1.exe
