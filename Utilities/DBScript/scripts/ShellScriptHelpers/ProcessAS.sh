# $1 = Month
# $2 = AS Hogg
# $3 = Start Date
# $4 = End Date

./ProcessASKV.sh $1 $2
./ProcessASTL.sh $1 $2 "$3" "$4"

