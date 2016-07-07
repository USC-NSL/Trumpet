This is the guide on how to replicate the result on matching delay.
- enable the matching time evaluation script
 - uncomment the following lines in checkflow method of receiver/flatreport.c
 - uint64_t s = rte_rdtsc();
 - fr->stat_matchdelay += rte_rdtsc() - s;
 - compile
- run the matching delay1.sh script provided in this folder in the receiver folder
- You can gather the result using delay1_result.sh

Now we compare the matching delay for four different ways of defining triggers:
- comment the following lines in receiver/basicfwd.c
 - flatreport_addtriggers(mt->fr, g.trigger_num, g.trigger_perpkt, g.trigger_patterns, types, 3);
- and add the following line for each of the triggers instead of the above definition choices
 - No matching: flatreport_makenotmatchingtriggers(mt->fr, g.trigger_num, g.trigger_patterns, types[0]);
 - Mathing on 8:
  - flatreport_makeallmatchingtriggers(mt->fr, 8, types[0]);
  - if (g.trigger_num > 8){
  - flatreport_makenotmatchingtriggers(mt->fr, g.trigger_num-8, g.trigger_patterns, types[0]);
  - }
 - 8 equal triggers: flatreport_makeperpktmatchingtriggers(mt->fr, g.trigger_num, g.trigger_patterns, types[0]);
 - 8 diff patterns: flatreport_makeperpktpatterntriggers(mt->fr, g.trigger_num, g.trigger_patterns, types[0]);
- Uncomment the following lines in the beginneing of lcore_main method in receiver/basicfwd.c
 - flatreport_profilematching(mt->fr);
 - flatreport_finish(mt->fr);
 - return 0;
- Compile
- Now you can use delay2.sh to run the experiment. Pass a parameter like "equaltrigger" to separate each config logs. Use delay2_result.sh to gather the result
