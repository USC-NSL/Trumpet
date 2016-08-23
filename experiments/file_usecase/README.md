This is a guide for how to use the general interface at the controller to add events from a file. The usecase is impelemented in usecase.c file

The file format is as follows:
- There are five fields separated by semicolon (;) per line. Each filed may have multiple items that are separated by comma. Here are the fields in order:
- Filter: A prefix based filter on 5-tuples (srcip, dstip, protocol, srcport, dstport). Tuple fields are separeted by comma, and each field must have a prefix length. For example, 10.0.0.1/32,10.1.1.0/24,0/0,0/0,0/0 means that the event will match source IP 10.0.0.1, and all destination IPs in prefix 10.1.1.0/24 on any source port, destination port and protocol.
- Predicate: The items are aggregate function, per-packet variable, and threshold. For now, Trumpet controller only supports summation aggregation function, and volume and packet as packet variables. 
- Flow granularity: It has 5 items, each specifying the flow granularity for the corresponding field in the filter.
- Time interval: in millisecond and a multiple of 10.

To run the controller with this usecase, 
- edit the controller/testevents.txt file with your events
- At the controller run: sudo -E ./build/tcpserver -c 0x5555 -n 4 --file-prefix=controller_ -m 128 -- -s 1 -l file -u 3
- At receiver run: : sudo -E build/basicfwd -c 0xaaaa -n 4 -- -t 0 -p 0 -P 32 -n 200000000 -d 0.000005 -c 192.168.0.1 -l file, where 192.168.0.1 is the controller IP
- Send the traffic to the receiver (may be using the provided sender)
