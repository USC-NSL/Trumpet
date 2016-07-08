# the columns will be prefix, # triggers, cycles used for sweeping

for f in `ls chunk_*_*_0.txt`
do
	echo `echo $f|cut -f 2,3 -d_|sed s/_/,/g`,`cat $f|grep mstepticks|cut -f11 -d\ `
done
