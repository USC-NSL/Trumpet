#!/bin/bash
cd /home/ubuntu/trumpet/sender
echo $1
sleep 5;
sudo -E ./build/basicfwd -c 0xaaaa -n4 --file-prefix=sender_ -m 128 -- $1


