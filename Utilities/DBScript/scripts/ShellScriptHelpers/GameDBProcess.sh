# $1 = Month
#
./ScanForItems.bat DR_DB_snapshot.hogg DR_DB_offline.hogg NW_Itemlist.txt
mv AuctionLotItemCounts.csv DR_AuctionLotItemCounts.csv
mv EntityguildbankItemCounts.csv DR_EntityGuildBankItemCounts.csv
mv EntityplayerItemCounts.csv DR_EntityPlayerItemCounts.csv
mv EntitysharedbankItemCounts.csv DR_EntitySharedBankItemCounts.csv
#
./ScanForItems.bat BH_DB_snapshot.hogg BH_DB_offline.hogg NW_Itemlist.txt
mv AuctionLotItemCounts.csv BH_AuctionLotItemCounts.csv
mv EntityguildbankItemCounts.csv BH_EntityGuildBankItemCounts.csv
mv EntityplayerItemCounts.csv BH_EntityPlayerItemCounts.csv
mv EntitysharedbankItemCounts.csv BH_EntitySharedBankItemCounts.csv
#
./ScanForItems.bat MF_DB_snapshot.hogg MF_DB_offline.hogg NW_Itemlist.txt
mv AuctionLotItemCounts.csv MF_AuctionLotItemCounts.csv
mv EntityguildbankItemCounts.csv MF_EntityGuildBankItemCounts.csv
mv EntityplayerItemCounts.csv MF_EntityPlayerItemCounts.csv
mv EntitysharedbankItemCounts.csv MF_EntitySharedBankItemCounts.csv
#
./ScanForItems.bat DU_DB_snapshot.hogg DU_DB_offline.hogg NW_Itemlist.txt
mv AuctionLotItemCounts.csv DU_AuctionLotItemCounts.csv
mv EntityguildbankItemCounts.csv DU_EntityGuildBankItemCounts.csv
mv EntityplayerItemCounts.csv DU_EntityPlayerItemCounts.csv
mv EntitysharedbankItemCounts.csv DU_EntitySharedBankItemCounts.csv
