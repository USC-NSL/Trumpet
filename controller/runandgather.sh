#!/bin/bash
RECEIVER_FOLDER=/home/masoud/dpdk_r/dpdk/examples/receiver
user=masoud
ssh -n  ${user}@$1 "sh -c 'cd \"$RECEIVER_FOLDER\"; \"$2\" \"$3\" >  \"$4\" 2>&1'"&
wait
scp ${user}@$1:$RECEIVER_FOLDER/$4 $4
