#!/bin/bash
p=1; 
w=4; 
i=10; 
a=2000; 
t=4096; 
r=14880000; 
d=0; 
f=300;
for t in 1 16 256 4096; 
	do 
	let t2=20*r; 
	./run.sh "-r $r -t $t2 -p 1 -D $d -l 1 -S 60 -a $a -f $f" "-t $t -p $p -P 1 -n $t2 -d 0.000005 -T 1 -l chunk_${1}_${t} -w $w -i $i"; 
	sleep 5;
done
