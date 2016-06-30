#!/bin/bash
p=8; 
w=4; 
i=10; 
a=1000; 
t=4096; 
r=14880000; 
d=0; 
f=300;
s=1500
for j in `seq 0 9`; 
	do for s in 350 400 450 500 550 600 650 700 750 800 850 900 950 1000;
		do 
			t2=$(echo ";10^10/(${s}+24)/8*20" |bc);
			let t240g=4*t2;
			./run40g.sh "-t $t2 -p 3 -D $d -l 1 -S $s -a $a -f $f" "-t $t -p $p -P 32 -n $t240g -d 0.000005 -T 1 -l 40gw_${s}_${j} -w $w -i $i" _${s}_${j}; 
			sleep 5;
			x=`cat 40gw_${s}_${j}_0.txt | grep "notfinished" | head -n 1 | cut -f3 -d\ `; 
			if [ $x -eq 0 ]; then 
				break; 
			fi;
	done; 
done

for j in `seq 0 9`; 
	do for s in 350 400 450 500 550 600 650 700 750 800 850 900 950 1000;
		do 
			echo `echo ${s},${j}`,`cat 40gw_${s}_${j}_0.txt | grep "RX Packet Loss" | head -n 1 | cut -f2 -d:| sed -e 's/ //g'`,`cat 40gw_${s}_${j}_0.txt | grep "notfinished" | head -n 1 | cut -f3,7 -d\ |sed -e 's/ /,/g'`;
	done; 
done
