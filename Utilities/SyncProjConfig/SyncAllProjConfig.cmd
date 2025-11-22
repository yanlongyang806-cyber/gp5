@echo off
pushd .
for /R %%d in (.) DO cd %%d && for %%e in (*.vcproj) do call c:\src\utilities\syncprojconfig\SyncProjFile.cmd %%e
popd
