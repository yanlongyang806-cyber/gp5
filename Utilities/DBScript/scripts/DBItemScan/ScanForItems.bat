date /t
time /t
C:\cryptic\tools\bin\DBScriptx64.exe -type EntityPlayer -script ScanEntitiesForItems.lua  -snapshot %1 -offlinehogg %2 -set infn %3
C:\cryptic\tools\bin\DBScriptx64.exe -type EntitySharedBank -script ScanEntitiesForItems.lua -snapshot %1 -offlinehogg %2 -set infn %3
C:\cryptic\tools\bin\DBScriptx64.exe -type EntityGuildBank -script ScanEntitiesForItems.lua -snapshot %1 -offlinehogg %2 -set infn %3
C:\cryptic\tools\bin\DBScriptx64.exe -type AuctionLot -script ScanAuctionLotsForItems.lua -snapshot %1 -offlinehogg %2 -set infn %3
date /t
time /t