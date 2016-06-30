#!/bin/bash
p=8; 
w=4; 
i=10; 
a=1000; 
t=4096; 
r=14880000; 
d=0; 
f=300;

for j in `seq 0 4`; 
	do for i in 20 10 5 1; 
		do for a in 16000 32000 64000; 
			do for p in 4 8 12 16; 
				do for r in 14880000 14500000 14000000 13500000 13000000 12500000 12000000 11500000 11000000 10500000 10000000 9500000 9000000 8500000 8000000 7500000 7000000 6500000 6000000 5000000;
					do 
					let t2=20*r; 
					let t=4096/8*p;
					./run.sh "-r $r -t $t2 -p 1 -D $d -l 1 -S 60 -a $a -f $f" "-t $t -p $p -P 32 -n $t2 -d 0.000005 -T 1 -c 204.57.3.177 -l feas_${i}_${a}_${p}_${r}_${j} -w $w -i $i" _${i}_${a}_${p}_${r}_${j}; 
					sleep 5;
                                        x=`cat feas_${i}_${a}_${p}_${r}_${j}_0.txt | grep "notfinished" | cut -f3 -d\ `;
                                        if [ $x -eq 0 ]; then
                                                break;
                                        fi;
				done; 
			done; 
		done; 
	done; 
done

# use to summarize the result
#for j in `seq 0 4`; 
#	do for i in 20 10 5 1; 
#		do for a in 16000 32000 64000; 
#			do for p in 4 8 12 16; 
#				do for r in 14880000 14500000 14000000 13500000 13000000 12500000 12000000 11500000 11000000 10500000 10000000 9500000 9000000 8500000 8000000 7500000 7000000 6500000 6000000 5000000;
#					do 
#					echo `echo ${i},${a},${p},${r},${j}`,`cat feas_${i}_${a}_${p}_${r}_${j}_0.txt | grep "RX Packet Loss" | cut -f2 -d:| sed -e 's/ //g'`,`cat feas_${i}_${a}_${p}_${r}_${j}_0.txt | grep "notfinished" | cut -f3,7 -d\ |sed -e 's/ /,/g'`;
#				done; 
#			done; 
#		done; 
#	done; 
#done
