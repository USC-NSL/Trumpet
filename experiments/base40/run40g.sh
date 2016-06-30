#!/bin/bash
sender1=192.168.0.1
sender2=192.168.0.3
sender1folder=/home/ubuntu/dpdk_r/dpdk/examples/sender
sender2folder=/home/ubuntu/dpdk_r/dpdk/examples/sender

echo "$1 --> $2"

sudo -E build/basicfwd -c 0xaaaa -n 4 -- $2 & 
sleep 15;
ssh -n -f ubuntu@${sender1} "sh -c 'nohup  \"$sender1folder\"/runsender.sh \"$1\" >  \"$sender1folder\"/temp.txt 2>&1 &'"
ssh -n -f ubuntu@${sender2} "sh -c 'nohup  /\"$sender2folder\"/runsender.sh \"$1\" >  \"$sender2folder\"/temp.txt 2>&1 &'"
sleep 55
x=`ps -aef | awk '{print $2,$8;}'| grep build/basicfwd | head -n 1| awk '{print $1;}'`
echo "kill $x"
sudo kill -s 2 $x
