In my setting, I had two sender machines, each having two 10G ports and the receiver had 4 10G ports.
This is how you can replicate the result in a similar setting:
- Make sure that you have assigned the ports to the DPDK driver
- Make sure that at each sender you edit the dstmac.txt so that it can set the MAC address of the generated packets correctly.
- Make sure that your 10G switch doesn't broadcast the packets. I did that by manually adding the host MAC addresses in its forwarding table.
- copy run40g.sh and 40g.sh files from this folder to the receiver folder
- set the correct IP of the senders at the run40g.sh  file
- run 40g.sh

The 40g.sh will run the experiment multiple times with different packet sizes and at the end shows a summary.
