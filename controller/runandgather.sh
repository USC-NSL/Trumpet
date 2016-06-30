#!/bin/bash
ssh -n  masoud@$1 "sh -c 'cd /home/masoud/dpdk_r/dpdk/examples/receiver; \"$2\" \"$3\" >  \"$4\" 2>&1'"&
wait
scp masoud@$1:/home/masoud/dpdk_r/dpdk/examples/receiver/$4 $4
