for p in 8 16 32 64; 
	do 
	for d in 0.88652 0.95908 0.98598 1; 
		do 
		for l in 10 20 30 40 50 60 70 80 90; 
		do 
			t2=$(echo ";10^10/((64*${d}+1500*(1-${d})+20)*8)*20" |bc); 
			echo $t2; 
			let t=l*64; 
			./run.sh "-t $t2 -p 1 -D $d -l $l" "-t 4096 -p 8 -P $p -n $t2 -d 0.000005 -T $t -l dosp_${p}_${d}_${l}"; 
			sleep 2; 
		done; 
	done; 
done
