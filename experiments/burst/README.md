This is the explanation on how to regenerate the experiment for comparing different packet burst rates. 
- you need to uncomment the part of the code that counts the CPU Quiescent time. It essentially is uncommenting what is related to lastzerots in lcore_main function of basicfwd.c file in the receiver folder. You may also look at the ../optimizations/lastzerots.patch file.
- copy burst.sh script into receiver folder and run it. 
- use burst_1.sh to summarize the result
