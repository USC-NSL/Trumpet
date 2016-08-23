#!/bin/bash
sender=204.57.3.147
senderfolder=/home/ubuntu/trumpet/sender
senderuser=ubuntu

echo "$1 --> $2"

sudo -E build/basicfwd -c 0xaaaa -n 4 -- $2 & 
ssh -n -f ${senderuser}@${sender} "sh -c 'nohup  \"$senderfolder\"/runsender.sh \"$1\" >  \"$senderfolder\"/temp.txt 2>&1 &'"
sleep 40
x=`ps -aef | awk '{print $2,$8;}'| grep build/basicfwd | head -n 1| awk '{print $1;}'`
echo "kill $x"
sudo kill -s 2 $x
