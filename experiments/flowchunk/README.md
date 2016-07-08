This is the guide for how to evaluate the benefit of chunking flows when the TPM sweeps over the triggers. 
We need to run the base10 experiment two times one with flow-chunks and one without it while changing the number of flows per trigger.
- Run flowchunk.sh script with parameter 0. It runs base10 experiemtn with different number of triggers. Triggers must cover the IP range so their definition will chang, so that the experiment with fewer triggers will have more flows per trigger. 
- Then set TRIGGERFLOW_BATCH to 1 in receiver/util.h, compile and run the script again with parameter 1
- Now use the gather.sh script to get the sweep time over the different runs
