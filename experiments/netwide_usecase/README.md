This is the guide on how to replicate the experiment for the network-wide usecase.
In this usecase, there are two servers that connect to the controller.
Each seerver has two 10G NICs and runs two parallel TPMs, each for a port.
We have a sender machine that sends traffic to those four ports using a single port
This is the workflow of the experiment:
- Upon joining of a TPM, the controller installs x events. The events are just packet counting with low threshold, so they will be satisfied every epoch
- Upon satisfaction at each TPM, the TPM sends a message to the controller.
- The controller polls all other TPMs
- The TPM reply to the poll and the controller pocesses them. 
- The controller and the TPMs create logs, which will be processed and gathered at the controller with a script.

To run the experiment:
- change the following flags at the receiver/basicfwd.c and compile
 - NOWORKER to 1
 - MEASUREMENTTHREAD_NUM to 2
 - MULTIPORT to 1
- Make sure you configured both ports at each TPM to use DPDK driver
- The generator will use four different queues at the NIC to send to those four ports at the TPMs. Set MULTIPORT to 0 in sender/basicfwd.c and compile it.
- I assume the controller IP is 192.168.0.1. Note that this means we are using out of band controller. but you can use in-band controller connection too, by enabling the receiver to use KNI (look at the congestion usecase). You can run the controller at the sender machine.
- I assume we want 16 events. You need to run the experiment for 4, 16, 64 and 256 events to get the result in the paper.
- At both receivers run:  sudo -E build/basicfwd -c 0xaaaa -n 4 -- -t 0 -p 0 -P 32 -n 297610000 -d 0.000005 -c 192.168.0.1 -l netwide_16
- At controller run: e=16; sudo -E ./build/tcpserver -c 0x5555 -n 4 --file-prefix=controller_ -m 128 -- -s 4 -l netwide_$e -e $e -u 3
- At sender you need to set the MAC address of the four ports at servers in the dstmac.txt. I used the dstmac.txt file provided in this folder
- At sender run: sudo -E build/basicfwd -c 0xaaaa -n 4  --file-prefix=sender_ -m 128 -- -t 800000 -p 15

Once the experiment finishes for all four different number of events, you can process and gather the logs at the controller using the following script.  
- At controller to gather logs
 - copy logprocess.sh from this folder to the receiver folder at the servers
 - make sure you update the IP addresses in getlogs.sh, and update folder and username at runandgather.sh files in the controller folder
 - run ./getlogs.sh netwide
 - The output log files will have the timestamps for reciving and sending all messages in the controller and TPMs. You can process the logs and generate the graph in the paper using the matlab script in dranetworkwide_doc.m file in this folder

At the end, make ssure you bring back the options in the receiver and the sender to the default one and compile
