#include "ddostable2.h"
#include "util.h"
#include "flatreport.h"
#include "stdlib.h"
#include "stdio.h"
#include <string.h>

#define COUNTERMASK 0XFE00
#define STEPMAX 0x0200
#define STEPMASK 0x01ff


inline void ddostable2_incpktnum(struct ddostable2 * dt, int inc){
	if (unlikely(((dt->pktnum + inc) & 0x1ffff) < (dt->pktnum & 0x1ffff))){//if processed 128k pkts
		uint64_t time = rte_rdtsc();
		if ((time - dt->laststeptime > dt->stepperiod)){
			dt->step = (dt->step + 1) & STEPMASK; //Ignore the first 5 bits
			dt->laststeptime = time;
//		LOG("ddostable inc step %"PRIu32", time %"PRIu64", pkts %"PRIu32", passed flows %"PRIu32"\n", dt->step, time, dt->pktnum, dt->fr->flownum);
		}
	}
	
	dt->pktnum += inc;
}

bool ddostable2_add(struct ddostable2 * dt, struct flatreport_pkt * pkt){
	uint16_t * buffer = (&dt->bufferstart) + (pkt->hash & dt->countarraymask);	
	//check step
	int16_t stepdiff = (int16_t)dt->step - (*buffer & STEPMASK);
	if (stepdiff > 1 || (stepdiff < 0 && (stepdiff + STEPMAX) > 1)){ //every two epoch
		*buffer &= STEPMASK; //reset counter
//		printf("reset %"PRIu32"\n", pkt->hash & dt->countarraymask);
	}

	//update step
	*buffer =  (*buffer & COUNTERMASK) | dt->step;

/*	flow_inlineprint(&pkt->f);
	printf("  %"PRIu32" %"PRIu32", %d, %u %u\n", pkt->hash & dt->countarraymask, dt->pktnum, stepdiff, (*buffer)/STEPMAX, (*buffer)&STEPMASK);*/

	//update counter, first check then update to avoid overloading the counter because of collisions
	if (*buffer >= dt->threshold_1){
	//	printf("%d Passed %p at %"PRIu32":%x > %x ",x, buffer, dt->pktnum, *buffer, dt->threshold_1);
	//	flow_print(&pkt->f);
		return true;
	}
	//increment counter
	*buffer += STEPMAX * (pkt->length/64 + (pkt->length%64 > 0?1:0));
	/*if (*buffer >= dt->threshold_1){
	//	printf("%d Passed %p at %"PRIu32":%x > %x ",x, buffer, dt->pktnum, *buffer, dt->threshold_1);
	//	flow_print(&pkt->f);
		return true;
	}*/
//	printf("inc %d %x %x %x\n", (pkt->length/64 + (pkt->length%64 > 0?1:0)), STEPMAX * (pkt->length/64 + (pkt->length%64 > 0?1:0)), *buffer, dt->threshold_1);
	return false;
}

// assumes 0 < threshold < 32
//countarraysize must be power of 2
struct ddostable2 * ddostable2_init(uint16_t threshold, uint32_t countarraysize){
	threshold = threshold/64 + (threshold%64 > 0?1:0);
	if (threshold > COUNTERMASK/STEPMAX || threshold == 0){
		fprintf(stderr, "too large ddos threshold or 0 threshold\n");
		exit(1);
	}
	struct ddostable2 * dt = BIGMALLOC (sizeof(struct ddostable2) + 2*(countarraysize-1));
	memset(&dt->bufferstart, 0, countarraysize);
	dt->countarraymask = countarraysize - 1;
	dt->threshold_1 = (threshold-1) * STEPMAX; //-1 because increment is done after condition check
	dt->pktnum = 0;
	dt->step = 0;
	dt->stepperiod = 10 * (uint64_t)rte_get_tsc_hz()/1e3;
	dt->laststeptime = rte_rdtsc();
	
	return dt;
}

void ddostable2_finish(struct ddostable2 * dt){
	BIGFREE(dt);
}

