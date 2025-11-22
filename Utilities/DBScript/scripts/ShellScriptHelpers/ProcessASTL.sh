# $1 = Month
# $2 = AS Hogg
# $3 = Start Date
# $4 = End Date

/cygdrive/c/Cryptic/tools/bin/DBScriptX64.exe -type Accountserver_Transactionlog -script trans_reporting_pt.lua -snapshot $2 -threads 3 -set fn AS_TL_$1.csv -set start_date "$3" -set end_date "$4"

