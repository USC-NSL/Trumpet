#!/bin/bash
p=1; 
w=4; 
i=10; 
a=1000; 
t=4096; 
r=12000000; 
d=0; 
f=300;
for P in 1 4 16 64; 
	do for t in 256 1024 4096 16384;
		do 
		let t2=20*r; 
		./run.sh "-r $r -t $t2 -p 1 -D $d -l 1 -S 60 -a $a -f $f" "-t $t -p $p -P $P -n $t2 -d 0.000005 -T 1 -l delay1_${P}_${t} -w $w -i $i"; 
		sleep 5;
	done; 
done
