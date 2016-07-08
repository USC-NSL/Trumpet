This guide explains how to replicate the data for DoS threshold prediction graph in the paper.
For this we run an experiment with different threshold and percentage of DoS traffic and different number of patterns for trigger filters (matching complexity). Then, we also profile the TPM to get some statistics on how long it takes to do some basic steps such as matching. Finally we use a matlab script to predict the threshold and compare the prediction with the result from experiments.
- To run the experiment, run dospredictscript.sh script provided in this folder in the receiver folder in the same setup as experiments/base10
- We need three types of statistics:
 - For matching time, look at how to get it from experiments/matching. You can get it for the 4 different number of wildcard patterns. You also need the stat for the case that a flow comes in and doesn't match with any of the triggers (used for DoS traffic). Use -s 1 paramter for the sender (first param of run.sh) to shift the generated traffic to not match the triggers.
 - for free cycles just after processing packets:
  - uncomment the following lines in receiver/basicfwd.c in lcore_main method
  - runForZerots(mt);
  - return 0;
  - uncomment the following line in receiver/flatreport.c in checkflow method
  - fr->lastpktisddos = true; return NULL;
  - compile and run the experiments/base10. You can get the number of free cycles in 20s experiment by `grep zertos log_0.txt`
 - for sweep time (cycles used to process trigger-flow pair). For this you need change receiver/triggertable2.c to add the right counters to gather the time used for sweeping in total and the number of flows swept. You can do that with global variables and update them in triggertable_finishsweep. Just gather type->ticksperupdate and type->tickperupdate_num. Then print them in triggertable_finish.
- To predict the threshold and draw the comparison diagram, run dosgraph.m provided in this folder. You may need to download columnlegend from https://www.mathworks.com/matlabcentral/fileexchange/27389-columnlegend
 - put the zerots numbers you got for 4 different dos traffic fraction in budget variable
 - put the matching and not matching cycles overhead in nomatch and matcch fields
 - put the sweep cycle overhea din sweep variable 
