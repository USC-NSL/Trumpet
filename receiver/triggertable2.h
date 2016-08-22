#ifndef TRIGGERTABLE2_H
#define TRIGGERTABLE2_H 1
#include "flow.h"
#include "flowentry.h"
#include "summary.h"
#include "util.h"
#include "matcher.h"

#define  TYPE_NAME_SIZE 20
#define VIOLATED_ENTRIES_SIZE 1024
#define TRIGGERTABLE_POSITION_TABLE_SIZE 32768

struct triggertable;
struct trigger;
struct trigger_pointer;


typedef void (*trigger_update_func)(struct trigger * t, void * d, struct triggertable * tt);
typedef bool (*trigger_report_func)(struct trigger * t, uint32_t stepsback, char * buf);
typedef void (*trigger_apply_func)(struct trigger * t, void * aux);
typedef bool (*trigger_condition_func)(struct trigger * t);


struct triggerflow{
	struct flow f;
//	struct flowentry * fe;
//	uint32_t hash;
};

/*
* A linklist of triggerflowlists. 
* These can be chained together to keep track of them in a linkedlist by themselves
*/
struct triggerflowlistpool{
	struct triggerflowlist * tfl;
	struct triggerflowlistpool * next;
};

struct triggerflowlist{
	uint64_t fullmap;//1 bits show an occupied entry in tf array
	struct triggerflow tf[TRIGGERFLOW_BATCH];
	struct triggerflowlist * next;
};

struct sweep_state{
        uint32_t index;
        uint32_t seen; // How many triggers have been seen till now
        struct triggerflowlist * tfl;
        struct triggerflowlist * tfl_last;
	bool triggerinterrupted;// If we are in the middle of processing flows for a trigger
};

struct triggertable{
	struct matcher * m;
	struct flatreport * fr;
	struct trigger ** violated_entries_buffer; //keep track of triggers to report (for strawman that sweeps over flow table instad of triggers)
	struct bitmap * trigger_pos_bm;
	struct triggerflowlist * freelist; //keeps track of free triggerflowlistss in a linkedlist
#if TRIGGERTABLE_INLINE_TRIGGER
        struct trigger * position_table;
#else
        struct trigger ** position_table;
#endif
	struct sweep_state state;
	struct triggertype * types;
	struct triggerflowlistpool * pools;
	struct triggertype * fgtype;
	uint16_t violated_entry_index; // for reporting when strawman goes over flow table entries
	uint16_t filled; //number of added triggers
	uint16_t lastid;
	void * triggers_temp[FLOWENTRY_TRIGGER_SIZE]; //used for strawman that sweeps over flow table entries
};

struct triggertype{
	struct summary ** s; // a trigger type may need multiple summaries
	trigger_update_func update_func;
	trigger_apply_func reset_func;
	trigger_condition_func condition_func;
	uint32_t ticksperupdate;
	uint32_t ticksperupdate2;
	uint16_t ticksperupdate_num; 
	uint16_t flow_micronum;
	summarymask_t summarymask; //Which summaries are necessary for this trigger?
	uint8_t id;
	struct triggertype * next;
	trigger_report_func report_func;
	trigger_apply_func free_func;
	trigger_apply_func print_func;
};

/*
* The general structure for triggers. Use the buffer buf to store trigger specific information like threshold.
*/
struct trigger{
	struct triggertype * type;
	uint32_t lastreset;//the epoch number when the trigger is reset	
	uint16_t pos;//index in the table of back-to-back triggers	
	uint16_t id;
	struct flow filter;	
	struct flow mask;
	uint32_t matched;
	uint16_t eventid;
	uint16_t reset_interval;
	uint8_t historyindex;//handles the history of aggregated values in the circular buffer of the trigger
#if TRIGGERTABLE_SWEEP
	uint8_t tfhead_filled;
	struct triggerflowlist * tfl;
	char buf[64] __attribute__((aligned(4)));
#else
	bool reported;	
	char buf[72] __attribute__((aligned(4)));
#endif
};

struct triggertable * triggertable_init(struct flatreport * fr);
void triggertable_finish(struct triggertable * tt);
void triggertable_print(struct triggertable * tt);

void triggertable_addtype(struct triggertable * tt, struct triggertype * type1);
uint16_t triggertable_gettypenum(struct triggertable * tt);
struct triggertype * triggertable_gettype(struct triggertable * tt, uint8_t type_id);

bool triggertable_addtrigger(struct triggertable * tt, struct trigger * t);
bool triggertable_removetrigger(struct triggertable * tt, struct trigger * t);

/*
* returns a new trigger in the trigger table
*/
struct trigger * triggertable_gettrigger(struct triggertable * tt);

/*
* Apply a function on every trigger
*/
void triggertable_applyontriggers(struct triggertable * tt, trigger_apply_func func, void * aux); 

/*
* For the strawman that sweeps over the flow table. Matches the flow and updates the flowentry
*/
void triggertable_match(struct triggertable * tt, struct flowentry * fe, struct summary_table * st);

/*
* The trigger matches the flow, so just let it know so that later it can aggregate its data.
*/
void triggertable_singletriggermatch(struct triggertable * tt, struct trigger * t, struct flowentry * fe, struct summary_table * st);

/*
* get a list of triggers that match a flow. Num should have the max number of triggers that can be put in the temptable. When the function is returned it num will be updated with the actual number of found triggers.
*/
void triggertable_justmatch(struct triggertable * tt, struct flow * f, struct flow * mask, struct trigger ** temptable, uint16_t * num);

/*
* Matching function when we want to sweep over triggers. It updates the flowentry accordingly
*/
void triggertable_sweepmatch(struct triggertable * tt, struct flowentry * fe, struct summary_table * st, uint32_t hash);

/*
* for strawman of sweeping over flows, this will aggregate statistics and updates the triggers corresponding to the flowentry 
*/
void triggertable_update(struct triggertable * tt, struct flowentry * fe, struct summary_table * st);

/*
* for packet history strawman, it will update the flowentry based on the packet directly
*/
void triggertable_update2(struct triggertable * tt, struct flowentry * fe, struct flatreport_pkt * pkt);

/*
* Sweep over the trigger table. If multiplestep is on, it respects the sweeptime and returns true if the sweep is finished. minsweepticks tells with what granularity check the time to not pass sweeptime.
*/
bool triggertable_sweep(struct triggertable * tt, uint32_t sweeptime, const uint32_t minsweepticks);

/*
* sweeping for packet history strawman. It assumes the statistics of triggers are already aggregated
*/
void triggertable_naivesweep(struct triggertable * tt);

/*
* Start sweeping over triggers
*/
void triggertable_startsweep(struct triggertable * tt);

/*
* returns true if the sweep over triggers finished
*/
bool triggertable_issweepfinished(struct triggertable * tt);

/*
* for sweeping over flows, this will collect all reports from triggers and print a report
*/
void triggertable_report(struct triggertable * tt);

/*
* Get the report for a trigger. It can return false if the data is not availbale for the requested time.
*/
bool triggertable_getreport(struct triggertable * tt, struct trigger * t, char * buf, uint32_t time);


void triggertable_parseflowgranularity(uint32_t flowgranularity, uint8_t* srcip_len, uint8_t* dstip_len, uint8_t* srcport_len, uint8_t* dstport_len, uint8_t* protocol_len);

struct triggertype * triggertype_init(uint16_t id, trigger_update_func update_func, trigger_report_func report_func, trigger_apply_func	free_func, trigger_apply_func reset_func, trigger_apply_func print_func, struct summary ** s, int summarynum, trigger_condition_func condition_func, uint32_t tickspersweep);
void triggertype_finish(struct triggertype * type);

void trigger_print2(struct trigger * t, void * aux);
void trigger_print(struct trigger * t, void * aux);
void trigger_cleantfl(struct trigger * t, struct triggertable * tt);

/*
* if the trigger matches the flow. Could be used for linear matching
*/
bool trigger_match(struct trigger * t, struct flow * f, struct flow * tempflow);

struct trigger * counter_trigger_init(struct trigger * t, uint16_t eventid, struct flow * filter, struct flow * mask, struct triggertype * type, uint32_t threshold, uint16_t timeinterval);
bool counter_trigger_report(struct trigger * t, uint32_t stepsback, char * buf);

void counter_trigger_free(struct trigger * t, void * aux);
void counter_trigger_reset(struct trigger * t, void * aux);
void counter_trigger_print(struct trigger * t, void * aux);
bool counter_trigger_condition(struct trigger * t);
void counter_trigger_historyprint(struct trigger * t);

void volume_trigger_update(struct trigger * t, void * d, struct triggertable * tt);
void pktnum_trigger_update(struct trigger * t, void * d, struct triggertable * tt);
void lossnum_trigger_update(struct trigger * t, void * d, struct triggertable * tt);
void congestion_trigger_update(struct trigger * t, void * data __attribute__((unused)), struct triggertable * tt);
//void congestion_trigger_reset(struct trigger * t, void * aux __attribute__((unused)));
void burst_trigger_update(struct trigger * t, void * d, struct triggertable * tt);
void burstloss_trigger_update(struct trigger * t, void * d, struct triggertable * tt);

struct trigger * fgcounter_trigger_init(struct trigger * t, uint16_t eventid, struct flow * filter, struct flow * mask, struct triggertype * type, uint32_t flowgranularity, struct triggertype * triggertype, uint32_t threshold, uint16_t timeinterval);
void fgcounter_trigger_reset(struct trigger * t, void * aux);
void fgcounter_trigger_print(struct trigger * t, void * aux);
bool fgcounter_trigger_condition(struct trigger * t);
bool fgcounter_trigger_report(struct trigger * t, uint32_t stepsback, char * buf);
void fgcounter_trigger_update(struct trigger * t, void * d, struct triggertable * tt);
	
#endif /* triggertable2.h */
