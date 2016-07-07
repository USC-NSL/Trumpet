# the columns will be # patterns, # triggers, delay per match in cycles

for f in `ls delay2_*_*_0.txt`
do
	echo `echo $f|cut -f 2,3 -d_|sed s/_/,/g`,`cat $f | grep 'Avg match' | cut -f4 -d\ `
done
