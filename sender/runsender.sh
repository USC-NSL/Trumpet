#!/bin/bash
cd $1
echo $2
sleep 5;
sudo -E ./build/basicfwd -c 0xaaaa -n4 --file-prefix=sender_ -m 128 -- $2
