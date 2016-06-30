#!/bin/bash
p=8; 
w=4; 
i=10; 
a=1000; 
t=4096; 
r=14880000; 
d=0; 
f=300;
for j in `seq 0 9`; 
	do for r in 14880000 14000000 12000000 10000000 8000000;
		do 
		let t2=20*r; 
		let t=4096/8*p;
		./runopt.sh "-r $r -t $t2 -p 1 -D $d -l 1 -S 60 -a $a -f $f" "-t $t -p $p -P 32 -n $t2 -d 0.000005 -T 1 -l opt_${1}_${r}_${j} -w $w -i $i" _${1}_${r}_${j}; 
		sleep 5;
	done; 
done
