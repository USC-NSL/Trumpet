#include "flatreport.h"
#include <stdio.h>
#include <stdlib.h> 
#include <stdint.h>
#include <string.h> 
#include <sys/time.h> // for nanosleep
#include <time.h> // timespec
#include <sys/socket.h>
#include <netinet/in.h>
#include <stddef.h> //for offsetof
#include <arpa/inet.h>
#include "bitmap.h"
#include "util.h"


#define FLATREPORT_INVALID_BURST_INDEX 16000

void * report(void * data);
int connect_to_server(struct flatreport * fr);

void finishsweep(struct flatreport * fr);
bool flatreport_entry_print(void * data, void * aux);
bool flatreport_entry_report(void * data, void * aux);
struct flowentry * checkflow(struct flatreport * fr, struct flatreport_pkt * pkt);
void flatreport_finishsweep(struct flatreport * fr);
void flatreport_addtriggers_profilematching(struct flatreport * fr, uint32_t triggernum, uint32_t patterns);
bool matchforatrigger(void * data, void * aux);
void preparematchanytrigger(struct flatreport * fr);
bool matchanytrigger(struct flatreport * fr, struct flatreport_pkt * pkt);

bool flatreport_entry_print(void * data, void * aux __attribute__((unused))) {
	struct flatreport_entry * fre = (struct flatreport_entry *) data;
	printf ("%u,%u:%u\n", fre->flow_id, fre->counter, fre->volume);
	return true;
}

//check if the flow is in table or not; need this for clean of ddos table
/*inline bool flatreport_isflowentryactive(struct flatreport * fr, struct flow * f){
	return hashmap_get2(fr->ft1, f, flow_hash(f), flowflowentry_equal, fr) != NULL;
}*/

inline void flatreport_readpacket_prefetch(struct flatreport * fr, struct flatreport_pkt * pkt){
	hashmap_prefetch(fr->ft1, pkt->hash);
}

struct matchforatriggerdata{
	struct flatreport * fr;
	struct trigger * t;
	struct flow f;
};

bool matchforatrigger(void * data, void * aux){
	struct matchforatriggerdata * d = (struct matchforatriggerdata *) aux;
	struct trigger * t = d->t;
	struct flatreport * fr = d->fr;
	struct flowentry * fe = (struct flowentry *)data;
	
	if (trigger_match(t, &fe->f, &d->f)){
//		flow_inlineprint(&fe->f);
//		printf(" match %d\n", t->id);
		singletriggermatch(fr->tt, t, fe, fr->st);
	}
	return true;
}

void flatreport_matchforatrigger(struct flatreport * fr, struct trigger * t){
	struct matchforatriggerdata d;
	d.t = t;
	d.fr = fr;
	hashmap_apply(fr->ft1, matchforatrigger, &d);
}

void preparematchanytrigger(struct flatreport * fr){
	fr->anymatchfilternum = 32;
	if (fr->anymatchfilternum > ANYMATCH_MAX){
		fprintf(stderr, "Flatreport: anymatch can only have %d flows, but is request %d \n", ANYMATCH_MAX, fr->anymatchfilternum);
		exit(1);
	}
	struct flow filter, filtermask;

	memset(fr->anymatchfilter, 0, sizeof(fr->anymatchfilter));
	memset(fr->anymatchfiltermask, 0xff, sizeof(fr->anymatchfiltermask));

/*	int i;
	for (i = 0; i < filternum - 1; i++){
		flow_fill(fr->anymatchfilter[0], filter);
		flow_fill(fr->anymatchfiltermask[0], filtermask);
	}*/

	int iprangesize = (1<<16) *2; //this is from the generator (number of unique IPs) #2 is becuase .4 in ip
	uint32_t dstip = (((((10<<8)+0)<<8)+4)<<8)+0;
	uint32_t srcip = (((((10<<8)+0)<<8)+5)<<8)+4;
	uint32_t ports =  (58513 <<16) | 2500;
	filtermask.dstip = htonl(0xffffffff << (log2_32(iprangesize)));
	filtermask.srcip = filtermask.ports = 0x00000000;

        filter.dstip = htonl(dstip) & filtermask.dstip;
        filter.srcip = htonl(srcip & filtermask.srcip);
        filter.ports = (htons((ports & filtermask.ports)>>16)<<16) | htons((ports & filtermask.ports) &0xffff);
	flow_fill(&fr->anymatchfilter[fr->anymatchfilternum - 1], &filter);
	flow_fill(&fr->anymatchfiltermask[fr->anymatchfilternum - 1], &filtermask);
	flow_print(&filter);
	flow_print(&filtermask);
}

inline bool matchanytrigger(struct flatreport * fr, struct flatreport_pkt * pkt){
	int i;
	for (i = 0; i < fr->anymatchfilternum; i++){
		if ((pkt->f.srcip & fr->anymatchfiltermask[i].srcip) == fr->anymatchfilter[i].srcip &&
			(pkt->f.dstip & fr->anymatchfiltermask[i].dstip) == fr->anymatchfilter[i].dstip &&
			(pkt->f.ports & fr->anymatchfiltermask[i].ports) == fr->anymatchfilter[i].ports){
			return true;
		}
	}
	return false;	
}


inline struct flowentry * checkflow(struct flatreport * fr, struct flatreport_pkt * pkt){
	fr->lastpktisddos = fr->dt != NULL && !ddostable2_add(fr->dt, pkt);
	if (fr->lastpktisddos){
		return NULL;
	}
//	fr->lastpktisddos = true; return NULL; //FIXME
//	uint64_t s = rte_rdtsc();
/*	if (!matchanytrigger(fr, pkt)){
		return NULL;
	}*/
	fr->stat_flownum++;
	struct flowentry * fe = hashmap_add2(fr->ft1, &pkt->f, pkt->hash, flowflowentry_equal, flowflowentry_init, fr);
#if TRIGGERTABLE_SWEEP
	triggertable_sweepmatch(fr->tt, fe, fr->st, pkt->hash);
	fe->lastupdate = fr->step;
#else
	triggertable_match(fr->tt, fe, fr->st);
        fe->summaries |= fr->pktnum_summary->mask;
#endif
/*	char buf[100];
	flow_inlineprint2(&pkt->f, buf);
	LOG("match %s %"PRIu64"\n", buf, rte_rdtsc());*/
//	fr->stat_matchdelay += rte_rdtsc() - s;
	return fe;
}

void flatreport_historyprocess(struct flatreport * fr){
        struct flatreport_pkt * pkt;
        struct flatreport_pkt * lastpkt = fr->pkts + fr->pkt_q - 1;
        fr->stat_pktnum += fr->pkt_q;
        for (pkt = fr->pkts; pkt <= lastpkt; pkt++){
		triggertable_update2(fr->tt, NULL, pkt);	
	}
	fr->pkt_q = 0;
}


void flatreport_process(struct flatreport * fr, struct flatreport_pkt * pkt){
        struct flowentry * fe = fr->last_flowentry;
	fr->stat_pktnum++;
	if (fr->dt!= NULL){
		ddostable2_incpktnum(fr->dt, 1);
	}
	 if (pkt->sameaslast && fr->lastpktisddos){
         	fe = checkflow(fr, pkt);
         }else if (fe == NULL || !pkt->sameaslast){ //the flowentry may have moved in the hashmap because of a sweep step in between (so fe could be null although it is same as before).
                        //first check flow table instead of ddostable
                fe = (struct flowentry *)hashmap_get2(fr->ft1, &pkt->f, pkt->hash, flowflowentry_equal, fr);
                if (fe == NULL){
                                //uint64_t s = rte_rdtsc();
                                fe = checkflow(fr, pkt);
                                //fr->stat_matchdelay += rte_rdtsc() - s;
                }
         }
         if (fe != NULL){
#if TRIGGERTABLE_SWEEP
         	if (fe->lastreset < fr->step){
                                //reset
			fe->lastreset = fr->step;
                	summary_apply(fr->st, fe, summary_apply_reset, fr);
                }
                fe->lastupdate = fr->step;
#endif
          	summary_apply_update(fr->st, fe, fr, pkt);
       	}
	fr->last_flowentry = fe;
}



void flatreport_batchprocess(struct flatreport * fr){
        struct flatreport_pkt * pkt;
        struct flatreport_pkt * lastpkt;
        struct flowentry * fe = fr->last_flowentry;
	lastpkt = fr->pkts + fr->pkt_q - 1;
        fr->stat_pktnum += fr->pkt_q;
	if (fr->dt != NULL){ 
		ddostable2_incpktnum(fr->dt, fr->pkt_q);
	}
        for (pkt = fr->pkts; pkt <= lastpkt; pkt++){
		if (pkt->sameaslast && fr->lastpktisddos){
                	fe = checkflow(fr, pkt);
                }else if (fe == NULL || !pkt->sameaslast){ //the flowentry may have moved in the hashmap because of a sweep step in between (so fe could be null although it is same as before).
		 	//first check flow table instead of ddostable to keep ddostable clean from valid flows
	                fe = (struct flowentry *)hashmap_get2(fr->ft1, &pkt->f, pkt->hash, flowflowentry_equal, fr);
        	        if (fe == NULL){
				fe = checkflow(fr, pkt);
			}    
		}
		if (fe != NULL){
#if TRIGGERTABLE_SWEEP
			if (fe->lastreset < fr->step){
				//reset
				fe->lastreset = fr->step;
			        summary_apply(fr->st, fe, summary_apply_reset, fr);
			}
			fe->lastupdate = fr->step;
#endif
	                summary_apply_update(fr->st, fe, fr, pkt);
		}
        }
        fr->last_flowentry = fe;
	fr->pkt_q = 0;
}

void flatreport_naivesweep(struct flatreport * fr){
	fr->step++;
	triggertable_naivesweep(fr->tt);
}

void flatreport_startsweep(struct flatreport * fr){
	fr->step++;	
//	LOG("%"PRIu64", 0, %d\n", rte_rdtsc(), fr->step);

	fr->stat_pktnum = 0;
	fr->stat_bursts = 0;
	flatreport_finishsweep(fr); //let's not do any sweep for this now
  #if TRIGGERTABLE_SWEEP
	triggertable_startsweep(fr->tt);	
  #else
	fr->sweepfinished = false;
	hashmap_iterator_init(fr->ft1, &fr->sweep_iterator);
	fr->sweep_removedentry = false;
  #endif

}

inline bool flatreport_issweepfinished(struct flatreport * fr){
#if TRIGGERTABLE_SWEEP
	return triggertable_issweepfinished(fr->tt);
#else
	return fr->sweepfinished;
#endif	
}

void flatreport_finishsweep(struct flatreport * fr){
        triggertable_report(fr->tt);
        //send_to_server(fr);
}

void flatreport_sweep(struct flatreport * fr, uint64_t sweeptime, uint64_t start __attribute__((unused))){
	fr->last_flowentry = NULL; //as flows may be deleted in this sweep step
#if TRIGGERTABLE_SWEEP
	if (triggertable_sweep(fr->tt, sweeptime, fr->minsweepticks)){
		//LOG("%"PRIu64", 1, %d\n", rte_rdtsc(), fr->step);
        //	send_to_server(fr);
	}
#else
	uint32_t swept, totalswept;
	struct flowentry * fe;

	uint32_t minsweeppkt = fr->minsweepticks/fr->ticksperentrysweep;
	if (minsweeppkt == 0){
		minsweeppkt = 1;
	}
	uint64_t current = start;

	totalswept = 0;	
	while (current < start + sweeptime && !fr->sweepfinished){
		//sweep for minsweeppkt items
		for (swept = 0; swept < minsweeppkt; swept++){
			fr->sweepfinished = !hashmap_iterator_next(&fr->sweep_iterator, (void **)&fe, fr->sweep_removedentry);
			if (unlikely(fr->sweepfinished)){
				hashmap_iterator_finish(&fr->sweep_iterator);
				flatreport_finishsweep(fr);
				break;
			}else{
				//pktnum is first summary on all flows
				fr->sweep_removedentry = summary_pktnum_get(fe->buf, fr) == 0;
        			if (fr->sweep_removedentry){
			                //remove the entry
			                flatreport_flowentry_finish(fe, fr);
			                hashmap_remove(fr->ft1, fe);
			        }else{
			                //update triggers
			                triggertable_update(fr->tt, fe, fr->st);
			                summary_apply(fr->st, fe, summary_apply_reset, fr);
			        }
			}
		}
#if MULTISTEP
		current = rte_rdtsc();
#endif
		totalswept += swept;
	}
	//current is always > start as we run this method at least for mimsweeptime
	if (likely(totalswept > 0)){
#if MULTISTEP
		fr->ticksperentrysweep = 0.4 * fr->ticksperentrysweep + (1-0.4) * (current-start)/totalswept;
#endif
	}
#endif
}


void flatreport_setminsweepticks(struct flatreport * fr, uint64_t minsweepticks){
	fr->minsweepticks = minsweepticks;
}

static __attribute__((unused)) bool flatreport_flowentryreplace (void * newdata __attribute__((unused)), void * data2 __attribute__((unused)), void * aux __attribute__((unused))){
#if TRIGGERTABLE_SWEEP
	struct flowentry * fe = (struct flowentry * ) data2;
	struct flatreport * fr = (struct flatreport *) aux;
	if (flowentry_isobsolete(fe, fr->step)){
/*		printf("at %d replace ", fr->step);
		flow_inlineprint(&fe->f);
		printf(" %d with ", fe->lastupdate);
		flow_inlineprint((struct flow *) newdata);
		printf("\n");*/
		flatreport_flowentry_finish(fe, fr);
		flowflowentry_init(newdata, data2, aux);
		return true;
	}else{
//		printf("at %d not replace ", fr->step);
/*		flow_inlineprint(&fe->f);
		printf(" %d with ", fe->lastupdate);
		flow_inlineprint((struct flow *) newdata);
		printf("\n");*/
	
	}
#endif
	return false;
}


inline void flatreport_report(struct flatreport * fr, struct trigger * t){
	client_sendsatisfactionsync(fr->c, t, fr->step);
//	client_sendtriggersync(fr->c, t, fr->step, true);
}

struct flatreport * flatreport_init(struct ddostable2 * dt, struct client * c){
	struct flatreport * fr; 

	fr = MALLOC(sizeof(struct flatreport));
	fr->ft1 = hashmap_init(1<<16, 1<<16, entry_size_64(sizeof(struct flowentry)),  offsetof(struct flowentry, e), (TRIGGERTABLE_SWEEP ? flatreport_flowentryreplace: NULL));

	fr->last_flowentry = NULL;
	
	fr->pkt_q = 0;
	fr->sweepfinished = true;
	fr->ticksperentrysweep = 1;
	fr->step = 1;
	fr->lastpktisddos = false;
	
	struct summary_table * st = fr->st = MALLOC(sizeof(struct summary_table));
	summary_init(st);

#if !TRIGGERTABLE_SWEEP && !PACKETHISTORY
	//thus pktnum is on all flows and has index 0
	fr->pktnum_summary = summary_hassummary(st, "pktnum");
	if (fr->pktnum_summary == NULL){
		fr->pktnum_summary = summary_pktnum_init("pktnum 0");
		if (summary_add(st, fr->pktnum_summary) == NULL){
			return fr;
		}
	}
#endif

	preparematchanytrigger(fr);

	//stats
	fr->stat_pktnum = 0;
	fr->stat_bursts = 0;
	fr->stat_matchdelay = 0;
	fr->stat_flownum = 0;
	
	//trigger table
	fr->tt = triggertable_init(fr);
	fr->dt = dt;
	if (dt != NULL){
		dt->fr = fr;
	}
	fr->c = c;
	fr->c->fr = fr;

	//client_test(c);

	fr->lf = NULL;
	
	//set_CPU(9);

	return fr;
}

inline bool flatreport_flowentry_finish(void * data, void *  aux){
	struct flowentry * fe = (struct flowentry *) data;
	struct flatreport * fr = (struct flatreport *) aux;
	summary_apply(fr->st, fe, summary_apply_finish, fr);
	memset(&fe->f, 0, sizeof(struct flow));
	memset(fe->buf, 0, FLOWENTRY_BUF_SIZE); //don't zero hashmap_elem
	return true;
}

inline struct flowentry * flatreport_getflowentry(struct flatreport * fr, struct flow * f){
	return hashmap_get2(fr->ft1, f, flow_hash(f), flowflowentry_equal, fr);
}
void flatreport_finish(struct flatreport * fr){
//	LOG("matched %d\n", flownum);
	LOG("match delay %f ns\n", fr->stat_matchdelay/2.3/fr->stat_flownum);
	LOG("Triggers print\n");
	triggertable_applyontriggers(fr->tt, trigger_print, fr);

	triggertable_finish(fr->tt);
	
	hashmap_apply(fr->ft1, flatreport_flowentry_finish, fr);
	hashmap_finish(fr->ft1);
	if (fr->dt != NULL){
		ddostable2_finish(fr->dt);
	}

	if (fr->c != NULL){
		client_finish(fr->c);
	}
	
	//do this at last
	summary_finish(fr->st);
	FREE(fr->st);

	if (fr->lf != NULL){
		summary_lossfinder_finish(fr->lf);
	}


	FREE(fr);
}


inline struct summary_table * flatreport_getsummarytable(struct flatreport * fr){
	return fr->st;
}

/*void send_to_server(struct flatreport * fr){
	int wrote0;
	if (fr->sockfd == 0){
		return;
	}
		
	uint32_t toSend = fr->tuples_buffer_current - fr->tuples_buffer
		  + fr->report_buffer_current-fr->report_buffer 
		  + fr->trigger_report_buffer_current - fr->trigger_report_buffer;
	toSend = 0;//TODO
	if (toSend == 0){
		wrote0 = write(fr->sockfd, (void *) &toSend, sizeof(uint32_t));
	}else{
		toSend += sizeof(struct flatreport_command);
		fr->command.flowdef_num = (fr->tuples_buffer_current - fr->tuples_buffer) / sizeof (struct flatreport_flowentry);
		fr->command.flowstat_num = (fr->report_buffer_current - fr->report_buffer) / sizeof (struct flatreport_entry);
		fr->command.trigger_buffusage = fr->trigger_report_buffer_current - fr->trigger_report_buffer;
		uint32_t wrote1=0, wrote2, wrote3;
		wrote1 += wrote2 = wrote3 = 0;
		wrote0 += write(fr->sockfd, (void *) &toSend, sizeof(uint32_t));
		wrote0 += write(fr->sockfd, (void *) &fr->command, sizeof(struct flatreport_command));
		if (fr->tuples_buffer_current > fr->tuples_buffer){
		  wrote1 = write(fr->sockfd, fr->tuples_buffer, fr->tuples_buffer_current-fr->tuples_buffer);
		}
		if (fr->report_buffer_current > fr->report_buffer){
			wrote2 = write(fr->sockfd, fr->report_buffer, fr->report_buffer_current-fr->report_buffer);
		}
		if (fr->trigger_report_buffer_current > fr->trigger_report_buffer){
			wrote3 = write(fr->sockfd, fr->trigger_report_buffer, fr->trigger_report_buffer_current-fr->trigger_report_buffer);
		}
//		printf ("wrote %d, %d, %d\n", wrote1, wrote2, wrote3);
	}
}*/


__attribute__((unused)) void flatreport_makenotmatchingtriggers(struct flatreport * fr, uint32_t triggernum, uint32_t patterns, struct triggertype * type){
	struct flow filter, filtermask;
	uint32_t triggernumtillnow = 0;

	filtermask.srcip = 0xffffffff;
	filter.srcip = 0;
	uint32_t ip = (((((10<<8)+0)<<8)+4)<<8)+0;
	uint32_t ports = 0;
	struct trigger * t = NULL;
	while (triggernumtillnow < triggernum){
		uint32_t mask1 = 0xffffffff;
		uint32_t mask2 = 0xffffffff;
		uint32_t patternid;
		for (patternid = 0; patternid < patterns && triggernumtillnow < triggernum; patternid++){
			//add a trigger
			filtermask.dstip = htonl(mask1);
			filtermask.ports = mask2;
			filter.dstip = htonl(ip & mask1);
			filter.ports = ports & mask2;
			if (t == NULL){
				t = triggertable_gettrigger(fr->tt);
			}
			t = counter_trigger_init(t, 0, triggernumtillnow, &filter, &filtermask, type, 80000000);
			if (!triggertable_addtrigger(fr->tt, t)){
				triggernumtillnow++;
			//	trigger_print2(t, NULL);
				t = NULL;
			}

			//update masks
			mask1<<=1;
			if (patternid % 32 == 31){
				mask1 = 0xffffffff;
				mask2 <<= 1;
			}
		}
		ip++;
		ports++;
	}
}

__attribute__((unused)) void flatreport_makeallpatternsmatchingtriggers(struct flatreport * fr, uint32_t triggernum, uint32_t patterns, struct triggertype * type){
	struct flow filter, filtermask;
	uint32_t triggernumtillnow = 0;

	filtermask.dstip = htonl((0xffffffff)>>(32 - log2_32(triggernum/patterns)));
	uint32_t ip = (((((128<<8)+0)<<8)+4)<<8)+0;
	uint32_t srcip = 1;
	uint32_t ports = (1<<16) | 2;
	struct trigger * t = NULL;
	while (triggernumtillnow < triggernum){
		uint32_t mask1 = 0xffffffff;
		uint32_t mask2 = 0xffffffff;
		uint32_t patternid;
		filter.dstip = htonl(ip) & filtermask.dstip;
		for (patternid = 0; patternid < patterns && triggernumtillnow < triggernum; patternid++){
			//add a trigger
			filtermask.srcip = htonl(mask1);
			filtermask.ports = mask2;
			filter.srcip = htonl(srcip & mask1);
			filter.ports = ports & mask2;
			if (t == NULL){
				t = triggertable_gettrigger(fr->tt);
			}
			t = counter_trigger_init(t, 0, triggernumtillnow, &filter, &filtermask, type, 80000000);
			if (!triggertable_addtrigger(fr->tt, t)){
				triggernumtillnow++;
			//	trigger_print2(t, NULL);
				t = NULL;
			}

			//update masks
			mask1 <<= 1;
			if (patternid % 32 == 31){
				mask1 = 0xffffffff;
				mask2 <<= 1;
			}
		}
		ip++;
	}
}

__attribute__((unused)) void flatreport_makeperpktmatchingtriggers(struct flatreport * fr, uint32_t triggernum, uint32_t patterns, struct triggertype * type){
	struct flow filter, filtermask;
	uint32_t triggernumtillnow = 0;
	uint16_t triggerperpkt = 8;
	uint16_t i; 

	filtermask.dstip = htonl((0xffffffff)>>(32 - log2_32(triggernum/triggerperpkt)));
	uint32_t ip = (((((128<<8)+0)<<8)+4)<<8)+0;
	uint32_t srcip = 1;
	uint32_t ports = (1<<16) | 2;
	struct trigger * t = NULL;
	while (triggernumtillnow < triggernum){
		uint32_t mask1 = 0xffffffff;
		uint32_t mask2 = 0xffffffff;
		uint32_t patternid;
		filter.dstip = htonl(ip) & filtermask.dstip;
		for (patternid = 0; patternid < patterns && triggernumtillnow < triggernum; patternid++){
			//add a trigger
			filtermask.srcip = htonl(mask1);
			filtermask.ports = mask2;
			filter.srcip = htonl(srcip & mask1);
			filter.ports = ports & mask2;

			for (i = 0; i < triggerperpkt; i++){
				t = triggertable_gettrigger(fr->tt);
				t = counter_trigger_init(t, 0, triggernumtillnow, &filter, &filtermask, type, 80000000);
				triggertable_addtrigger(fr->tt, t);
				triggernumtillnow++;
//				trigger_print2(t, NULL);
			}

			//update masks
			mask1 <<= 1;
			if (patternid % 32 == 31){
				mask1 = 0xffffffff;
				mask2 <<= 1;
			}
			ip++;
		}
	}
}


__attribute__((unused)) void flatreport_makeperpktpatterntriggers(struct flatreport * fr, uint32_t triggernum, uint32_t patterns, struct triggertype * type){
	struct flow filter, filtermask;
	uint32_t triggernumtillnow = 0;
	uint16_t triggerperpkt = 8;
	uint16_t i; 

	filtermask.dstip = htonl((0xffffffff)>>(32 - log2_32(triggernum/triggerperpkt)));
	uint32_t ip = (((((128<<8)+0)<<8)+4)<<8)+0;
	uint32_t srcip = 1;
	uint32_t ports = (1<<16) | 2;
	struct trigger * t = NULL;
	uint32_t patternid = 0;
	uint32_t mask1 = 0xffffffff;
	uint32_t mask2 = 0xffffffff;
	while (triggernumtillnow < triggernum){
		filter.dstip = htonl(ip) & filtermask.dstip;
		for (i = 0; i < triggerperpkt; i++){
			if (patternid % patterns == 0){
				mask1 = 0xffffffff;
				mask2 = 0xffffffff;
			}
			filtermask.srcip = htonl(mask1);
			filtermask.ports = mask2;
			filter.srcip = htonl(srcip & mask1);
			filter.ports = ports & mask2;

			t = triggertable_gettrigger(fr->tt);
			t = counter_trigger_init(t, 0, triggernumtillnow, &filter, &filtermask, type, 80000000);
			triggertable_addtrigger(fr->tt, t);
			triggernumtillnow++;
//			trigger_print2(t, NULL);

			mask1 <<= 1;
			if (patternid % 32 == 31){
				mask1 = 0xffffffff;
				mask2 <<= 1;
			}
			patternid++;
		}
		ip++;
	}
}



__attribute__((unused)) void flatreport_makeallmatchingtriggers(struct flatreport * fr, 
uint32_t triggernum, struct triggertype * type){
	struct flow filter, filtermask;
	uint32_t triggernumtillnow = 0;

//	uint32_t ip = (((((128<<8)+0)<<8)+4)<<8)+0;
	filtermask.srcip = htonl(0x00000000);
	filtermask.dstip = htonl(0x00000000);
	filter.srcip = 0;
	uint32_t ports = (1<<16) | 2;
	filter.dstip = 0;
	struct trigger * t = NULL;
	uint32_t mask1 = 0xffffffff;
	while (triggernumtillnow < triggernum){
		filtermask.ports = mask1;
		filter.ports = ports & mask1;
		//add a trigger
		if (t == NULL){
			t = triggertable_gettrigger(fr->tt);
		}
		t = counter_trigger_init(t, 0, triggernumtillnow, &filter, &filtermask, type, 80000000);
		if (!triggertable_addtrigger(fr->tt, t)){
			triggernumtillnow++;
//			trigger_print2(t, NULL);
			t = NULL;
		}
		mask1 <<= 1;
	}
}

void flatreport_addtriggers_profilematching(struct flatreport * fr, uint32_t triggernum, uint32_t patterns){
	struct summary * summaries [1];
	summaries[0] = summary_pktnum_init("pktnum");
	if (summary_add(fr->st, summaries[0]) == NULL){
		return ;
	}
	
	struct triggertype * type =  triggertype_init(0, pktnum_trigger_update, counter_trigger_report, counter_trigger_free, counter_trigger_reset, counter_trigger_print, 1, summaries, 1, counter_trigger_condition, 30);
	triggertable_addtype(fr->tt, type);
//	flatreport_makeallpatternsmatchingtriggers(fr, triggernum, patterns, type);
//	flatreport_makeperpktmatchingtriggers(fr, triggernum, patterns, type);
//	flatreport_makeperpktpatterntriggers(fr, triggernum, patterns, type);

	flatreport_makeallmatchingtriggers(fr, 8, type);	
	if (triggernum > 8){
		flatreport_makenotmatchingtriggers(fr, triggernum-8, patterns, type);
	}
}

static void cleantfl(struct trigger * t, void * aux){
	trigger_cleantfl(t, (struct triggertable *) aux);
}


void flatreport_profilematching(struct flatreport * fr){
//	flatreport_addtriggers_profilematching(fr, triggerNum, patterns);
	//LOG("tables %"PRIu16"\n", triggertable_gettablenum(fr->tt));
	uint32_t i, num;
	num = 1<<27;
	struct flatreport_pkt * pkt = fr->pkts;
	uint64_t clocksum = 0;
	
	int j;
	for (j = 0; j < 1; j++){
/*		pkt->f.dstip = htonl((((((10<<8)+0)<<8)+4)<<8)+0);
		pkt->f.srcip = htonl((((((10<<8)+0)<<8)+5)<<8)+4);
		pkt->f.ports =  htons(58513)<<16 | htons(2500);*/
		pkt->f.srcip = htonl(1);
        	pkt->f.dstip = htonl((((((128<<8)+0)<<8)+4)<<8)+0);
	        pkt->f.ports = (1<<16) | 2;
        	pkt->ts = 0;
	        pkt->seq = ntohl(0x283902);
        	pkt->length = 1;
	        pkt->ip_p = 0;
	        pkt->sameaslast = false;
        	pkt->hash = flow_hash(&pkt->f);
		uint64_t start_tsc = rte_rdtsc();
		for (i = 0; i < num; i++){
//			checkflow(fr, pkt);
//				start_tsc = rte_rdtsc();
fr->stat_flownum++;
	struct flowentry * fe = hashmap_add2(fr->ft1, &pkt->f, pkt->hash, flowflowentry_equal, flowflowentry_init, fr);
#if TRIGGERTABLE_SWEEP
	triggertable_sweepmatch(fr->tt, fe, fr->st, pkt->hash);
	fe->lastupdate = fr->step;
#else
	triggertable_match(fr->tt, fe, fr->st);
        fe->summaries |= fr->pktnum_summary->mask;
#endif
//				clocksum += rte_rdtsc() - start_tsc;

			if ((i & 0xffff) == 0xffff){
				pkt->f.dstip = htonl((((((10<<8)+0)<<8)+4)<<8)+0);
				pkt->f.srcip = htonl(ntohl(pkt->f.srcip)+1);
			}else{
				pkt->f.dstip = htonl(ntohl(pkt->f.dstip)+1);
			}
			pkt->hash = flow_hash(&pkt->f);
			if (((i+1) & 0x07fff) < (i & 0x07fff)){
				fr->step++;
			}
			if ((i & 0x1FFFF) == 0x1FFFF){
				clocksum += rte_rdtsc() - start_tsc;
				triggertable_applyontriggers(fr->tt, cleantfl, fr->tt);
				start_tsc = rte_rdtsc();
			}
		}
		clocksum += rte_rdtsc() - start_tsc;
		LOG("Avg match cycles %f\n", (double)clocksum/num);
	}
	triggertable_applyontriggers(fr->tt, trigger_print, fr);
}

void flatreport_addtypes(struct flatreport * fr, struct triggertype ** types, int num __attribute__((unused))){
	int j;
	//uint16_t types_num = 3; //trigger_perpkt > SUMMARIES_NUM? SUMMARIES_NUM: trigger_perpkt;
	struct summary * summaries [1];

	summaries[0] = summary_hassummary(fr->st, "pktnum 0");
	if (summaries[0] == NULL){	
		summaries[0] = summary_pktnum_init("pktnum 0");
        	if (summary_add(fr->st, summaries[0]) == NULL){
	        	return;
		}
	}
	j = 0;
	types[j] =  triggertype_init(j, pktnum_trigger_update, counter_trigger_report, counter_trigger_free, counter_trigger_reset, counter_trigger_print, 1, summaries, 1, counter_trigger_condition, 100);
	triggertable_addtype(fr->tt, types[j]);	

	j++;
	summaries[0] = summary_hassummary(fr->st, "volume");
	if (summaries[0] == NULL){	
		summaries[0] = summary_volume_init("volume");
        	if (summary_add(fr->st, summaries[0]) == NULL){
	        	return;
		}
	}
	types[j] =  triggertype_init(j, volume_trigger_update, counter_trigger_report, counter_trigger_free, counter_trigger_reset, counter_trigger_print, 1, summaries, 1, counter_trigger_condition, 100);
	triggertable_addtype(fr->tt, types[j]);	

	j++;
	summaries[0] = summary_hassummary(fr->st, "lossnum");
	if (summaries[0] == NULL){	
		summaries[0] = summary_lossnum2_init("lossnum");
        	if (summary_add(fr->st, summaries[0]) == NULL){
	        	return;
		}
	}
	types[j] =  triggertype_init(j, lossnum_trigger_update, counter_trigger_report, counter_trigger_free, counter_trigger_reset, counter_trigger_print, 1, summaries, 1, counter_trigger_condition, 200);
	triggertable_addtype(fr->tt, types[j]);	

	j++;
	struct summary * summaries2 [2];
	summaries2[0] = summary_hassummary(fr->st, "ack");
	if (summaries2[0] == NULL){	
		summaries2[0] = summary_ack_init("ack");
        	if (summary_add(fr->st, summaries2[0]) == NULL){
	        	return;
		}
	}
	summaries2[1] = summary_hassummary(fr->st, "syn");
	if (summaries2[1] == NULL){	
		summaries2[1] = summary_syn_init("syn");
        	if (summary_add(fr->st, summaries2[1]) == NULL){
	        	return;
		}
	}
	types[j] =  triggertype_init(j, congestion_trigger_update, counter_trigger_report, counter_trigger_free, counter_trigger_reset, counter_trigger_print, 1, summaries2, 2, counter_trigger_condition, 100);
	triggertable_addtype(fr->tt, types[j]);	
}


void flatreport_addtriggers(struct flatreport * fr, uint16_t trigger_num, uint16_t trigger_perpkt, uint16_t patterns, struct triggertype ** types, int types_num){
	if (trigger_num == 0 || trigger_perpkt == 0){
		return;
	}
	int iprangesize = (1<<16); //this is from the generator (number of unique IPs)
	if (trigger_num < trigger_perpkt){
		fprintf(stderr, "# triggers (%d) was smaller than # trigger per packet (%d)\n", trigger_num, trigger_perpkt);
		trigger_num = trigger_perpkt;
	}else if (trigger_num > iprangesize * trigger_perpkt){
		//trigger_num must be smaller than range * trigger_perpkt
		fprintf(stderr, "# triggers (%d) was larger than # trigger per packet (%d) * range (%d)\n", trigger_num, trigger_perpkt, iprangesize);
		trigger_num = trigger_perpkt * iprangesize;
	}
	int i;		
	struct triggertype * type;
	
	struct flow filter, filtermask;
	struct in_addr a;
	
	//inet_aton("255.255.255.240", &a);
	inet_aton("0.0.0.0", &a);
	int uniquetriggers = trigger_num/trigger_perpkt;
	uint32_t bits = log2_32(iprangesize/uniquetriggers); //need to cover iprange with these unique triggers
	/*int levels = patterns;
	if (levels == 0){
		levels = 1;
	}
	if (levels > trigger_perpkt){
		levels=trigger_perpkt;
	}*/

	//make pktnum trigger type


	/*
	char buf[SUMMARY_NAME_LEN];
	for (j = 0; j < types_num; j++){
		snprintf(buf, SUMMARY_NAME_LEN, "pktnum %d", j);
		summaries[0] = summary_hassummary(fr->st, buf);
	        if (summaries[0] == NULL){	
	        	summaries[0] = summary_pktnum_init(buf);
        	        if (summary_add(fr->st, summaries[0]) == NULL){
	        		return;
			}
		}
		types[j] =  triggertype_init(j, pktnum_trigger_update, counter_trigger_report, counter_trigger_free, counter_trigger_reset, counter_trigger_print, 1, summaries, 1, counter_trigger_condition, 30);
		triggertable_addtype(fr->tt, types[j]);	
	}*/

	//make loss finder trigger
/*	fr->lf = summary_lossfinder_init();
	summaries[0] = summary_hassummary(fr->st, "lossnum");
        if (summaries[0] == NULL){
                summaries[0] = summary_lossnum_init(fr->lf, "lossnum");
                if (summary_add(fr->st, summaries[0]) == NULL){
			return;
                }
        }
	types[1] =  triggertype_init(1, lossnum_trigger_update, counter_trigger_report, counter_trigger_free, counter_trigger_reset, counter_trigger_print, 1, summaries, 1, 30);
	triggertable_addtype(fr->tt, types[1]);*/

	

	int js [uniquetriggers];
	int j;	
	for (j = 0; j < uniquetriggers; j++){
		js[j] = j;
	}

	filtermask.dstip = htonl(0xffffffff<<bits);
	uint32_t dstip = (((((10<<8)+0)<<8)+4)<<8)+0;
	uint32_t srcip = (((((10<<8)+0)<<8)+5)<<8)+4;
	uint32_t ports =  (58513 <<16) |2500;
	struct trigger * t = NULL;
	uint32_t patternid = 0;
	uint32_t mask1 = 0x00000000;
	uint32_t mask2 = 0xffffffff;

	for (j = 0; j< uniquetriggers; j++){
                filter.dstip = htonl(dstip + (js[j]<<bits)) & filtermask.dstip;
                for (i = 0; i < trigger_perpkt; i++){
			type = types[(j * trigger_perpkt + i) % types_num];
                        filtermask.srcip = htonl(mask1);
                        filtermask.ports = (htons(mask2>>16)<<16) | htons(mask2 &0xffff);
                        filter.srcip = htonl(srcip & mask1);
                        filter.ports = (htons((ports & mask2)>>16)<<16) | htons((ports&mask2) &0xffff);

                        t = triggertable_gettrigger(fr->tt);
                        t = counter_trigger_init(t,0, j*trigger_perpkt + i, &filter, &filtermask, type, 80000000);
                        triggertable_addtrigger(fr->tt, t);
		//	trigger_print2(t, NULL);

                        mask2 <<= 1;
                        patternid++;

                        if (patternid % 32 == 0){
                                mask1 = ~((~mask1)>>1); //add one to the left
                                mask2 = 0xffffffff;
                        }
			if (patternid % patterns == 0){ // reset patterns
				mask2 = 0xffffffff;
				mask1 = 0x00000000;
			}
                }
        }

/*	for (i = 0; i < trigger_perpkt/levels; i++){
		if (levels == 1){
//			shuffle(js, uniquetriggers, sizeof(int));
		}
		for (k = 0; k < levels; k++){
			int triggers_num = uniquetriggers; //each level will cover 1<<level times of level 1 triggers
			type = types[((i * levels) + k) % types_num];
			if (triggers_num == 0){
				triggers_num = 1;
			};
			for (j = 0; j < triggers_num; j++){
				uint32_t a_mask = //0xFFFFFFFF;
				 0x00000000;
				filtermask.srcip = htonl(a_mask);
				a_mask = 0xFFFFFFFF << bits; //shift based on bits to cover the ip range with these number of uniquetriggers
				filtermask.dstip = htonl(a_mask);
				filtermask.ports = htonl(0xFFFF0000<<k);
				//doesn' tmatter it is masked
				filter.srcip = //ihtonl((((((10<<8)+0)<<8)+4)<<8)+3); 
				0;
				uint32_t b = ((((10<<8)+0)<<8)+4)<<8;
				b = b + (js[j]<<bits);
				b &= a_mask;
				a.s_addr = htonl(b);
				filter.dstip = a.s_addr;
				//printf("%s\n", inet_ntoa(a));
				filter.ports = htons(2500)<<16 & filtermask.ports;
				
				struct trigger * t = triggertable_gettrigger(fr->tt);
				t = counter_trigger_init(t,id++, &filter, &filtermask, type, 80000000);
			//	trigger_print2(t, NULL);
				triggertable_addtrigger(fr->tt, t);
			}
		}
	}*/
	//LOG("tables %"PRIu16"\n", triggertable_gettablenum(fr->tt));
}

