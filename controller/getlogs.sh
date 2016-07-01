#!/bin/bash
logname=$1;
ip1=192.168.0.2
ip2=192.168.0.3
for t in 0 1; 
	do 
	for e in 4 16 64 256; 
		do 
		./runandgather.sh $ip1 
		./logprocess.sh ${logname}_${e}_${t}.txt ${logname}_${e}_${t}.log; 
	done; 
done

# use 2 and 3 for the logs on the second server
for t in 0 1; 
	do 
	let t2=t+2; 
	for e in 4 16 64 256; 
		do 
		./runandgather.sh $ip2 
		./logprocess.sh ${logname}_${e}_${t}.txt ${logname}_${e}_${t2}.log; 
	done; 
done

# process the controller logs
for e in 4 16 64 256; 
	do 
	./logprocess.sh ${logname}_${e}.txt > ${logname}_${e}_c.log; 
done
