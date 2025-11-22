# $1 = Month
# $2 = TL File
echo "Processing NW"
cat $2 | gawk -F, '$8 == 24 && $13 == "Cashpurchase" {tot += $15} END {printf "NW.PaidZenTransfer,%s\n",tot}' > NW_TLData_$1.csv
cat $2 | gawk -F, '$8 == 15 && $11 == "NW" && $13 == "Exchange" {tot += $15} END {printf "NW.SharedZenToExchange,%s\n",tot}' >> NW_TLData_$1.csv
cat $2 | gawk -F, '$8 == 24 && $7 == "CR" && $13 == "Customerservice" {tot += $15} END {printf "NW.PaidZenCSGrant,%s\n",tot}' >> NW_TLData_$1.csv
cat $2 | gawk -F, '$8 == 24 && $7 == "DR" && $13 == "Customerservice" {tot += $15} END {printf "NW.PaidZenCSRemove,%s\n",tot}' >> NW_TLData_$1.csv
cat $2 | gawk -F, '$8 == 24 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "NW.PaidZenRecognize,%s\n",tot}' >> NW_TLData_$1.csv
cat $2 | gawk -F, '$8 == 49 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "NW.PaidZenEscrowRecognize,%s\n",tot}' >> NW_TLData_$1.csv
cat $2 | gawk -F, '$8 == 52 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "NW.PaidZenClaimRecognize,%s\n",tot}' >> NW_TLData_$1.csv

echo "Processing ST"
cat $2 | gawk -F, '$8 == 17 && $13 == "Cashpurchase" {tot += $15} END {printf "ST.PaidZenTransfer,%s\n",tot}' > ST_TLData_$1.csv
cat $2 | gawk -F, '$8 == 21 && $13 == "Cashpurchase" {tot += $15} END {printf "ST.PaidSteamZen,%s\n",tot}' >> ST_TLData_$1.csv
cat $2 | gawk -F, '$8 == 15 && $11 == "STO" && $13 == "Exchange" {tot += $15} END {printf "ST.SharedZenToExchange,%s\n",tot}' >> ST_TLData_$1.csv
cat $2 | gawk -F, '$8 == 17 && $7 == "CR" && $13 == "Customerservice" {tot += $15} END {printf "ST.PaidZenCSGrant,%s\n",tot}' >> ST_TLData_$1.csv
cat $2 | gawk -F, '$8 == 17 && $7 == "DR" && $13 == "Customerservice" {tot += $15} END {printf "ST.PaidZenCSRemove,%s\n",tot}' >> ST_TLData_$1.csv
cat $2 | gawk -F, '$8 == 17 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "ST.PaidZenRecognize,%s\n",tot}' >> ST_TLData_$1.csv
cat $2 | gawk -F, '$8 == 21 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "ST.PaidSteamZenRecognize,%s\n",tot}' >> ST_TLData_$1.csv
cat $2 | gawk -F, '$8 == 29 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "ST.PaidZenEscrowRecognize,%s\n",tot}' >> ST_TLData_$1.csv
cat $2 | gawk -F, '$8 == 28 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "ST.PaidSteamZenEscrowRecognize,%s\n",tot}' >> ST_TLData_$1.csv
cat $2 | gawk -F, '$8 == 34 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "ST.PaidZenClaimRecognize,%s\n",tot}' >> ST_TLData_$1.csv
cat $2 | gawk -F, '$8 == 33 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "ST.PaidSteamZenClaimRecognize,%s\n",tot}' >> ST_TLData_$1.csv
cat $2 | gawk -F, '$8 == 15 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "SHARED.PaidZenRecognize,%s\n",tot}' >> ST_TLData_$1.csv

echo "Processing CO"
cat $2 | gawk -F, '$8 == 19 && $13 == "Cashpurchase" {tot += $15} END {printf "CO.PaidZenTransfer,%s\n",tot}' > CO_TLData_$1.csv
cat $2 | gawk -F, '$8 == 22 && $13 == "Cashpurchase" {tot += $15} END {printf "CO.PaidSteamZen,%s\n",tot}' >> CO_TLData_$1.csv
cat $2 | gawk -F, '$8 == 15 && $11 == "CO" && $13 == "Exchange" {tot += $15} END {printf "CO.SharedZenToExchange,%s\n",tot}' >> CO_TLData_$1.csv
cat $2 | gawk -F, '$8 == 19 && $7 == "CR" && $13 == "Customerservice" {tot += $15} END {printf "CO.PaidZenCSGrant,%s\n",tot}' >> CO_TLData_$1.csv
cat $2 | gawk -F, '$8 == 19 && $7 == "DR" && $13 == "Customerservice" {tot += $15} END {printf "CO.PaidZenCSRemove,%s\n",tot}' >> CO_TLData_$1.csv
cat $2 | gawk -F, '$8 == 19 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "CO.PaidZenRecognize,%s\n",tot}' >> CO_TLData_$1.csv
cat $2 | gawk -F, '$8 == 22 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "CO.PaidSteamZenRecognize,%s\n",tot}' >> CO_TLData_$1.csv
cat $2 | gawk -F, '$8 == 38 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "CO.PaidZenEscrowRecognize,%s\n",tot}' >> CO_TLData_$1.csv
cat $2 | gawk -F, '$8 == 37 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "CO.PaidSteamZenEscrowRecognize,%s\n",tot}' >> CO_TLData_$1.csv
cat $2 | gawk -F, '$8 == 43 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "CO.PaidZenClaimRecognize,%s\n",tot}' >> CO_TLData_$1.csv
cat $2 | gawk -F, '$8 == 42 && $7 == "DR" && $13 == "Recognition" {tot += $16} END {printf "CO.PaidSteamZenClaimRecognize,%s\n",tot}' >> CO_TLData_$1.csv

echo "Processing ByDay"
cat $2 | gawk -F, -f trans_date_breakdown.gawk > NW_SalesByDay_$1.csv
cat $2 | gawk -F, -f trans_date_breakdown.gawk -v game="STO" > ST_SalesByDay_$1.csv
cat $2 | gawk -F, -f trans_date_breakdown.gawk -v game="CO" > CO_SalesByDay_$1.csv

echo "Processing ByUnits"
cat $2 | gawk -F, -f trans_breakdown.gawk > NW_UnitsSold_$1.csv
cat $2 | gawk -F, -f trans_breakdown.gawk -v game="STO" > STO_UnitsSold_$1.csv
cat $2 | gawk -F, -f trans_breakdown.gawk -v game="CO" > CO_UnitsSold_$1.csv
