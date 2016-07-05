This guide explains how to replicate the data for DoS threshold prediction graph in the paper.
For this we run an experiment with different threshold and percentage of DoS traffic and different number of patterns for trigger filters (matching complexity). Then, we also profile the TPM to get some statistics on how long it takes to do some basic steps such as matching. Finally we use a matlab script to predict the threshold and compare the prediction with the result from experiments.
- To run the experiment, run dospredictscript.sh script provided in this folder in the receiver folder in the same setup as experiments/base10
- To get the statistics:
- To predict the threshold and draw the comparison diagram, run dosgraph.m provided in this folder.  
