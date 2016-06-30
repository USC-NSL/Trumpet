# the columns will be burst, run number, total cycles, free cycles, cycles used for sweeping

for f in `ls burst_*_*_0.txt`
do
	echo `echo $f|cut -f 2,3 -d_|sed s/_/,/g`,`cat $f | grep cycles | cut -f3 -d\ | cut -f2 -d:|sed s/,//g`,`cat $f|grep zerots|cut -f6 -d\ |cut -f2 -d:`,`cat $f|grep mstepticks|cut -f11 -d\ `
done
