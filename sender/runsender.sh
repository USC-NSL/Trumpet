#!/bin/bash
cd /home/ubuntu/dpdk_r/dpdk/examples/sender
export RTE_SDK=/home/ubuntu/dpdk_r/dpdk
export RTE_TARGET=x86_64-native-linuxapp-gcc
echo $1
sleep 5;
sudo -E ./build/basicfwd -c 0xaaaa -n4 --file-prefix=sender_ -m 128 -- $1


