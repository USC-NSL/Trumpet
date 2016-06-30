This is the explanation on how to regenerate the experiment for comparing different optimizations. 
- copy the scripts in this folder to the receiver folder
- you need to uncomment the part of the code that counts the CPU Quiescent time. It essentially is uncommenting what is related to lastzerots in lcore_main function of basicfwd.c file in the receiver folder. You may also look at the lastzerots.patch file in this folder.
- the optimizations.sh script will run the experiment multiple times with different packet rates to find out if it is feasible with a rate or not.
- Workflow: we disable each optimization in util.h file, compile the code and run the optimizations.sh <prefix>. Each setting can have a prifix, so that later running optimizations_1.sh can gather the result easily.
- The mapping between the optimizations and the option in util.h
-- Packet prefetcing: PKT_PREFETCH_ENABLE
-- Hashmap prefetching: HASH_PREFETCH_ENABLE
-- Prefetching in the sweep phase: SWEEP_PREFETCH_ENABLE
-- using hugepages: DPDK_BIG_MALLOC
-- putting triggers back to back: TRIGGERTABLE_INLINE_TRIGGER

Example: 
- set PKT_PREFETCH_ENABLE to 0
- compile
- ./optimizations.sh 1
- ./optimizations_1.sh
