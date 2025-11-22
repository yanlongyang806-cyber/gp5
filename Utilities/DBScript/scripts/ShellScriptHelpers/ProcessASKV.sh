# $1 = Month
# $2 = AS Hogg

/cygdrive/c/Cryptic/tools/bin/DBScriptX64.exe -type Account -script kv_new.lua -snapshot $2 -threads 3 -set fn AS_KV_$1.csv

