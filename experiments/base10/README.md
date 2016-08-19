This is the guide on how to repeat the simplest experiment: sending and receivinng 10G traffic without any DoS attack.Note that you need two machines sender and receiver.
- At the receiver make sure that the options at util.h and basicfwd.c are set correctly (default ones)
- At the receiver, edit run.sh and set sender IP, senderfolder and sender user correctly (Note the IP is different from the receiver IP).
- Also in sender/runsender.sh fix the path in the cd command
- At the receiver run: ./run.sh "-t 200000000 -S 60" "-t 4096 -p 8 -P 32 -n 200000000 -d 0.000005 -T 1 -w 4 -i 10 -l base10"

This will set the sender to receive 200M small packets and the receiver to expect 200M packets, forward to 4 workers, report every 10ms and fill its trigger repository with 4k triggers with 32 patterns in a way that every packet may match 8 triggers.
You can see the help of the parameters for the receiver, sender or the controller usin the -h command. For example, at receiver you can run, sudo ./build/basicfwd -c aaaa -n 4 -- -h

