for j in `seq 0 4`;
do
	for d in 0.72254 0.85421 0.90946 0.93985 0.95908 0.97234 0.98204 0.98945 0.99528 1; 
		do 
		for l in 1 10 20 30 40 50 60; 
			do 
			t2=$(echo ";10^10/((64*${d}+1500*(1-${d})+20)*8)*20" |bc); 
			echo $t2; 
			let t=l*64; 
			./run.sh "-t $t2 -p 1 -D $d -l $l" "-t 4096 -p 8 -P 32 -n $t2 -d 0.000005 -T $t -l dos_${d}_${l}_${j}"; 
			sleep 2; 
		done; 
	done;
done

#columns will be dos packet ratio, # dos packets per flow, run number
for f in `find . -name dos_\*_\*_\*_0.txt`; do echo `echo $f | cut -f 2,3,4 -d_ |sed 's/_/,/g'`,`grep 'RX Packets' $f | cut -f 2 -d: | cut -f 2 -d\ | sed 's_/_,_g'`,`grep 'notfinished' $f |cut -f 3 -d\ `; done >ddosmeasure.txt
