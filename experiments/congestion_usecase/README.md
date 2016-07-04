This is the guide for replicating the result for the congestion root cause detection usecase in the paper. 
We use two machines as traffic generators, called A and B, and one as receiver and controller, called C. Running both the receiver and the controller at the same server allows us to avoid time synchronization error for the diagrams.
The scenario is as follows: C installs the Congestion detection triggers at A and B. A and B will send TCP traffic to C. A also sends UDP traffic after a few seconds. As soon as the UDP traffic starts, A and B see congestion and inform the controller at C. The controller installs another event at C to detect the UDP traffic reactively. 
This is called the reactive scenario becasue the controller waits for the congestion event from A and B to install the third event. We also test it a proactive scenario that the controller installs the third event, to detect heavy UDP, from the beginning. We run both cases in one usecase because we need to compare the latency of reactive and proactive detection in one diagram.

- Use htb to limit the rate at C
 - sudo modprobe ifb
 - sudo modprobe act_mirred
 - ext_ingress=ifb0
 - sudo ifconfig $ext_ingress up
 - sudo tc qdisc add dev $ext_ingress root handle 1: htb default 1
 - sudo tc class add dev $ext_ingress parent 1: classid 1:1 htb rate 100mbit
- At receiver/basicfwd.c 
 - set the USEKNI option to 1. 
 - Uncomment the following lines in receiver/flatreport.c, in the checkflow method. This is to print the time for the first time the receiver sees a flow. We will explain it later, when we draw the diagram.
  - char buf[100];
  - flow_inlineprint2(&pkt->f, buf);
  - LOG("match %s %"PRIu64"\n", buf, rte_rdtsc());
 - Compile. You should use this code on all the three servers.
- Start the controller: sudo -E build/tcpserver -c 0x5555 -n 4 --file-prefix=controller_ -m 128 -- -s 2 -l proudp -e 1 -u 1
- I assume the IP of the controller at C is 192.168.0.1
- Run the receiver code at the three servers:
 - A: sudo -E build/basicfwd -c 0xaaaa -n 4 -- -t 0 -p 0 -P 32 -n 297610000 -d 0.000005 -c 192.168.0.1 -l proudp
 - B: sudo -E build/basicfwd -c 0xaaaa -n 4 -- -t 0 -p 0 -P 32 -n 297610000 -d 0.000005 -c 192.168.0.1 -l proudp
 - C: sudo -E build/basicfwd -c 0xaaaa -n 4 --file-prefix=receiver_ -m 128 -- -t 0 -p 0 -P 32 -n 297610000 -d 0.000005 -c 192.168.0.1 -l proudp
- Forward the traffic received at C on the KNI interface to the rate limiter: sudo ifconfig vEth0 192.168.1.3/24; sleep 5; ext=vEth0; ext_ingress=ifb0; sudo tc qdisc add dev $ext handle ffff: ingress; sudo tc filter add dev $ext parent ffff: protocol all u32 match u32 0 0 action mirred egress redirect dev $ext_ingress
- Run iper at the receiver (C): iperf -s -p 2500 & iperf -s -u -p 2501 &
- Run tcp dump at the receiver (C): sudo tcpdump -i vEth0 -w proudp.dump
- Run Iperf at the centers for TCP and UDP traffic. Try to run these two commands at the same time:
 - A: sudo ifconfig vEth0 192.168.1.1/24; sleep 5; iperf -c 192.168.1.3 -t 20 -p 2500 & sleep 15; iperf -c 192.168.1.3 -b 100m -n 512k -p 2501
 - B: sudo ifconfig vEth0 192.168.1.2/24; sleep 5; iperf -c 192.168.1.3 -t 20 -p 2500
- Wait about a minute until the experiment finishes. Then stop the tcpdump at C

Now we gather the logs and convert the tcpdump trace.
- At machine C, assuming that the IP of A and B are 192.168.0.2 and 192.168.0.3, run the following in the controller folder. Make sure you set the user and the receiver folder right in the runandgather.sh file
 - copy experiments/netwide_usecase/logprocess.sh into the receiveer folder on all servers
 - fname=proudp
 - ./logprocess.sh ${fname}.txt > ${fname}_c.log
 - ./runandgather.sh 192.168.0.2 ./logprocess.sh ${fname}_0.txt ${fname}_1.log;
 - ./runandgather.sh 192.168.0.3 ./logprocess.sh ${fname}_0.txt ${fname}_0.log;
 - ./runandgather.sh 192.168.0.1 ./logprocess.sh ${fname}_0.txt ${fname}_2.log;
- Now we use tshark to convert the tcpdump file and get the throughput data from each connection. 
 - Install wireshark on the controller
 - tshark -T fields -n -r udp/${fname}.dump -E separator=, -e frame.time_relative -e ip.src -e frame.len -e ip.proto  > ${fname}_trace.txt
 - grep 192.168.1 ${fname}_trace.txt | sed -e 's/192.168.1.1/1/g' -e 's/192.168.1.2/2/g' -e 's/192.168.1.3/3/g' > ${fname}_trace2.txt

- To draw the diagram in the paper you need the four log files generated above: proudp_c.log, proudp_0.log, proudp_1.log, proudp_2.log and the throughput data proudp_trace2.txt
- You also need a kind of base for synchronizing the log files and the throughput data from tcpdump. For this we use the time that the receiver (C) got the udp data. 
 - Remember we made the change receiver folder to print the time, the TPM sees a flow for the first time. Now we get that for the UDP flow. Run the following at the receiver folder at machine C
 - grep 'match .*,192.168.1.3:2501' proudp_0.txt
- Now open the udpthroughputdiagram.m file in this folder. Copy the timestamp from the above command to logudp variable. 
- Run udpthroughputdiagram.m matlab script. You may edit the file to pass the four log files and the trace file.
