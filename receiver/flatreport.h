#ifndef FLATREPORT_H
#define FLATREPORT_H 1

#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include "hashmap.h"
#include "triggertable2.h"
#include "flowentry.h"
#include "ddostable2.h"
#include "client.h"

#define ANYMATCH_MAX 32

struct flatreport_entry{
	uint32_t counter;
	uint32_t volume;
	uint16_t flow_id;
};

struct flatreport_flowentry{
	struct flow f;
	uint16_t flow_id;
};

struct flatreport_command{
	uint32_t trigger_buffusage;
	uint16_t flowdef_num;
	uint16_t flowstat_num;
};

struct flatreport_pkt{
	struct flow f;
	uint32_t ts;
	uint32_t seq;
	uint32_t ack;
	uint32_t hash;
	uint16_t length;
	uint8_t ip_p; 
	bool sameaslast;
};

struct flatreport{
//per packet
	uint32_t stat_pktnum;
	uint32_t step;
	uint8_t pkt_q; //how many packets are in the queue for cache prefetching
	bool lastpktisddos;
	uint32_t stat_bursts;
	struct ddostable2 * dt;
	struct flowentry * last_flowentry;
	struct hashmap * ft1;
	struct summary_table * st;
	uint64_t epoch_ts;
	struct flatreport_pkt pkts [FLATREPORT_PKT_BURST];
//per match
	struct summary * pktnum_summary;
	struct triggertable * tt;
	uint64_t stat_matchdelay;
	uint32_t stat_flownum;
	uint8_t anymatchfilternum;
	struct flow anymatchfilter[ANYMATCH_MAX];  
	struct flow anymatchfiltermask[ANYMATCH_MAX];  
////////////////////////////////sweep	
	bool sweepfinished;
	bool sweep_removedentry;
	uint32_t ticksperentrysweep;
	uint32_t minsweepticks;
	struct hashmap_iterator sweep_iterator;
//rare
// report	
	struct client * c;
//init	
	struct lossfinder * lf;

};
void flatreport_readpacket_prefetch(struct flatreport * fr, struct flatreport_pkt * pkt);
void flatreport_batchprocess(struct flatreport * fr);
void flatreport_readpacket(struct flatreport * fr);
struct flatreport * flatreport_init(struct ddostable2 * dt, struct client * c);
void flatreport_finish(struct flatreport * fr);
void flatreport_startsweep(struct flatreport * fr);
void flatreport_addtriggers(struct flatreport * fr, uint16_t trigger_num, uint16_t trigger_perpkt, uint16_t patterns, struct triggertype ** types, int types_num);
void flatreport_addtypes(struct flatreport * fr, struct triggertype ** types, int num);
void flatreport_sweep(struct flatreport * fr, uint64_t sweeptime, uint64_t start);
struct flowentry * flatreport_getflowentry(struct flatreport * fr, struct flow * f);
void flatreport_setminsweepticks(struct flatreport * fr, uint64_t minsweepticks);
bool flatreport_flowentry_finish(void * data, void * aux);
bool flatreport_issweepfinished(struct flatreport * fr);
void flatreport_profilematching(struct flatreport * fr);
void flatreport_naivesweep(struct flatreport * fr);
struct summary_table * flatreport_getsummarytable(struct flatreport * fr);
void flatreport_historyprocess(struct flatreport * fr);
void flatreport_process(struct flatreport * fr, struct flatreport_pkt * pkt);
void flatreport_report(struct flatreport * fr, struct trigger * t);
void flatreport_matchforatrigger(struct flatreport * fr, struct trigger * t);

#endif /* flatreport.h */
