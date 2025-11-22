# $1 = Month
# $2 = KV File
#
echo "Processing NW" 
cat $2 | gawk -F, '$2 == "PaidNeverwinterZen" {tot += $3} END {printf "PaidNWZen,%d\n",tot}' > NW_KVData_$1.csv 
cat $2 | gawk -F, '$2 == "PaidNeverwinterEscrow" {tot += $3} END {printf "PaidNWEscrow,%d\n",tot}' >> NW_KVData_$1.csv 
cat $2 | gawk -F, '$2 == "PaidNeverwinterClaim" {tot += $3} END {printf "PaidNWClaim,%d\n",tot}' >> NW_KVData_$1.csv 
cat $2 | gawk -F, '$2 == "NW.Live:Renametokens" {tot += $3} END {printf "NW.RenameTokens,%s\n",tot}' >> NW_KVData_$1.csv 
cat $2 | gawk -F, '$2 == "NW.Live:RespecTokens" {tot += $3} END {printf "NW.RespecTokens,%s\n",tot}' >> NW_KVData_$1.csv 
cat $2 | gawk -F, '$1 ~ /40257025908843$/' | grep "PaidNeverwinterClaim" >> NW_KVData_$1.csv 
cat $2 | gawk -F, '$1 ~ /93783405930454$/' | grep "PaidNeverwinterClaim" >> NW_KVData_$1.csv 
echo "Processing STO" 
cat $2 | gawk -F, '$2 == "PaidStarTrekZen" {tot += $3} END {printf "PaidSTZen,%d\n",tot}' > ST_KVData_$1.csv 
cat $2 | gawk -F, '$2 == "PaidSteamStarTrekZen" {tot += $3} END {printf "PaidSTSteamZen,%d\n",tot}' >> ST_KVData_$1.csv 
cat $2 | gawk -F, '$2 == "PaidStarTrekEscrow" {tot += $3} END {printf "PaidSTEscrow,%d\n",tot}' >> ST_KVData_$1.csv 
cat $2 | gawk -F, '$2 == "PaidSteamStarTrekEscrow" {tot += $3} END {printf "PaidSTSteamEscrow,%d\n",tot}' >> ST_KVData_$1.csv 
cat $2 | gawk -F, '$2 == "PaidStarTrekClaim" {tot += $3} END {printf "PaidSTClaim,%d\n",tot}' >> ST_KVData_$1.csv 
cat $2 | gawk -F, '$2 == "PaidSteamStarTrekClaim" {tot += $3} END {printf "PaidSTSteamClaim,%d\n",tot}' >> ST_KVData_$1.csv 
echo "Processing CO" 
cat $2 | gawk -F, '$2 == "PaidChampionsZen" {tot += $3} END {printf "PaidCOZen,%d\n",tot}' > CO_KVData_$1.csv 
cat $2 | gawk -F, '$2 == "PaidSteamChampionsZen" {tot += $3} END {printf "PaidCOSteamZen,%d\n",tot}' >> CO_KVData_$1.csv 
cat $2 | gawk -F, '$2 == "PaidChampionsEscrow" {tot += $3} END {printf "PaidCOEscrow,%d\n",tot}' >> CO_KVData_$1.csv 
cat $2 | gawk -F, '$2 == "PaidSteamChampionsEscrow" {tot += $3} END {printf "PaidCOSteamEscrow,%d\n",tot}' >> CO_KVData_$1.csv 
cat $2 | gawk -F, '$2 == "PaidChampionsClaim" {tot += $3} END {printf "PaidCOClaim,%d\n",tot}' >> CO_KVData_$1.csv 
cat $2 | gawk -F, '$2 == "PaidSteamChampionsClaim" {tot += $3} END {printf "PaidCOSteamClaim,%d\n",tot}' >> CO_KVData_$1.csv 
echo "Done"
