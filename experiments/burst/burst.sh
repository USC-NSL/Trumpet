#!/bin/bash
p=8; 
w=4; 
i=10; 
a=1000; 
t=4096; 
r=12000000; 
d=0; 
f=300;
b=1;
for j in `seq 0 9`; 
	do for b in 1 2 4 8 16 32 64;
		do 
		let t2=20*r; 
		let t=4096/8*p;
		./run.sh "-r $r -t $t2 -p 1 -D $d -l 1 -S 60 -a $a -f $f -b $b" "-t $t -p $p -P 32 -n $t2 -d 0.000005 -T 1 -l burst_${b}_${j} -w $w -i $i" _${b}_${j}; 
		sleep 5;
	done; 
done
