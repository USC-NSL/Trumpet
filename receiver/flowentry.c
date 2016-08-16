#include "flowentry.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void flowentry_print(void * data);

struct flowentry * flowentry_init(void){
	struct flowentry * fe = (struct flowentry *) malloc (sizeof(struct flowentry));
	memset(fe, 0, sizeof(struct flowentry));
	return fe;
}

void flowentry_finish(struct flowentry * fe){
	free(fe);
}

bool flowentry_print2(uint16_t id __attribute__((unused)), void * data, void * aux __attribute__((unused))){
  	struct flowentry * fe = (struct flowentry *) data;
	flow_inlineprint(&fe->f);
	printf(" %x\n", fe->summaries);
/*	printf(" volume: %llu, counter: %d, loss: ", (long long unsigned)fe->volume, fe->counter);
	losslisthead_apply(&fe->loss, losslist_print, NULL);
	printf(" burst: ");
	burstlisthead_apply(&fe->burst, burstlist_print, NULL);
	printf("\n");*/
	return true;
}

void flowentry_print(void * data){
	struct flowentry * fe = (struct flowentry *) data;
	flow_print(&fe->f);
}

inline bool flowentry_equal(void * data1, void * data2, void * aux __attribute__((unused))){ 
	struct flowentry * fe1 = (struct flowentry *) data1;
	struct flowentry * fe2 = (struct flowentry *) data2;
	return flow_equal(&fe1->f, &fe2->f);
}

bool flowflowentry_equal(void * data, void * data2, void * aux __attribute__((unused))){
        return flow_equal(&((struct flowentry *)data2)->f, (struct flow *)data);
}

void flowflowentry_init(void * data, void * data2, void * aux __attribute__((unused))){
//	memset(data2, 0, sizeof(struct flowentry)); assume it is clean
        flow_fill(&((struct flowentry *)data2)->f, (struct flow *)data);
}


inline bool flowentry_isobsolete(struct flowentry * fe __attribute__((unused)), uint32_t step __attribute__((unused))){
#if TRIGGERTABLE_SWEEP
	//return step > 1000 && fe->lastupdate < step - 1000;
	return fe->lastupdate < step - 1 && step > 0;
#endif
	return false;
}
