#!/bin/bash
sender=204.57.3.177
senderfolder=/home/ubuntu/dpdk_r/dpdk/examples/sender

echo "$1 --> $2"

sudo -E perf stat -e instructions,r53ff24,r530108,r532008,r534008,dTLB-load-misses,L1-dcache-loads,L1-dcache-load-misses,cache-references,cache-misses,page-faults -D 6000 -o perf${3}.txt ./build/basicfwd -c 0xaaaa -n 4 -- $2 &
#sudo -E build/basicfwd -c 0xaaaa -n 4 -- $2 & 
ssh -n -f ubuntu@${sender} "sh -c 'nohup  \"$senderfolder\"/runsender.sh \"$1\" >  \"$senderfolder\"/temp.txt 2>&1 &'"
sleep 40
x=`ps -aef | awk '{print $2,$8;}'| grep build/basicfwd | head -n 1| awk '{print $1;}'`
echo "kill $x"
sudo kill -s 2 $x
