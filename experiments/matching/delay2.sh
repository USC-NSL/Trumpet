#!/bin/bash
p=1; 
t=4096; 
for P in 1 4 16 64; 
	do
	sudo -E build/basicfwd -t $t -p $p -P $P -T 1 -l delay2_${1}_${P}; 
done
