#!/bin/bash
export RTE_SDK=/home/`whoami`/dpdk_r/dpdk
export RTE_TARGET=x86_64-native-linuxapp-gcc
sender=192.168.0.1
senderfolder=/home/ubuntu/dpdk_r/dpdk/examples/sender

echo "$1 --> $2"

sudo -E build/basicfwd -c 0xaaaa -n 4 -- $2 & 
ssh -n -f ubuntu@${sender} "sh -c 'nohup  \"$senderfolder\"/runsender.sh \"$1\" >  \"$senderfolder\"/temp.txt 2>&1 &'"
sleep 40
x=`ps -aef | awk '{print $2,$8;}'| grep build/basicfwd | head -n 1| awk '{print $1;}'`
echo "kill $x"
sudo kill -s 2 $x
