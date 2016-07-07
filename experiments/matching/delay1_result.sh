# the columns will be # patterns, # triggers, delay per match in ns

for f in `ls delay1_*_*_0.txt`
do
	echo `echo $f|cut -f 2,3 -d_|sed s/_/,/g`,`cat $f | grep 'match delay' | cut -f3 -d\ `
done
