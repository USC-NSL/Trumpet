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
	struct flowentry * fe;
	uint32_t hash;
};

struct triggerflowlistpool{
	struct triggerflowlist * tfl;
	struct triggerflowlistpool * next;
};

struct triggerflowlist{
	uint64_t fullmap;
	struct triggerflow tf[TRIGGERFLOW_BATCH];
	struct triggerflowlist * next;
};

struct sweep_state{
        uint32_t index;
        uint32_t seen;
        struct triggerflowlist * tfl;
        struct triggerflowlist * tfl_last;
	bool triggerinterrupted;
};

struct triggertable{
	struct matcher * m;
	struct flatreport * fr;
	struct trigger ** violated_entries_buffer;
	struct bitmap * trigger_pos_bm;
	struct triggerflowlist * freelist;
#if TRIGGERTABLE_INLINE_TRIGGER
        struct trigger * position_table;
#else
        struct trigger ** position_table;
#endif
	struct sweep_state state;
	struct triggertype * types;
	struct triggerflowlistpool * pools;
	uint16_t violated_entry_index;
	uint16_t filled;
	void * triggers_temp[FLOWENTRY_TRIGGER_SIZE];
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
	uint16_t reset_interval;
	summarymask_t summarymask;
	uint8_t summarynum;
	uint8_t id;
	struct triggertype * next;
	trigger_report_func report_func;
	trigger_apply_func free_func;
	trigger_apply_func print_func;
};

struct trigger{
	struct triggertype * type;
	uint32_t lastupdate;	
	uint16_t pos;	
	uint16_t id;
	struct flow filter;	
	struct flow mask;
	uint32_t matched;
	uint16_t eventid;
	uint8_t historyindex;
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
void triggertable_addtype(struct triggertable * tt, struct triggertype * type1);
uint16_t triggertable_gettypenum(struct triggertable * tt);
bool triggertable_addtrigger(struct triggertable * tt, struct trigger * t);
bool triggertable_removetrigger(struct triggertable * tt, struct trigger * t);
void triggertable_match(struct triggertable * tt, struct flowentry * fe, struct summary_table * st);
void triggertable_update(struct triggertable * tt, struct flowentry * fe, struct summary_table * st);
void triggertable_update2(struct triggertable * tt, struct flowentry * fe, struct flatreport_pkt * pkt);
bool triggertable_sweep(struct triggertable * tt, uint32_t sweeptime, const uint32_t minsweepticks);
void triggertable_sweepmatch(struct triggertable * tt, struct flowentry * fe, struct summary_table * st, uint32_t hash);
void singletriggermatch(struct triggertable * tt, struct trigger * t, struct flowentry * fe, struct summary_table * st);
void triggertable_startsweep(struct triggertable * tt);
bool triggertable_issweepfinished(struct triggertable * tt);
void triggertable_report(struct triggertable * tt);
void triggertable_print(struct triggertable * tt);
void triggertable_applyontriggers(struct triggertable * tt, trigger_apply_func func, void * aux); 
struct trigger * triggertable_gettrigger(struct triggertable * tt);
void triggertable_naivesweep(struct triggertable * tt);
void triggertable_justmatch(struct triggertable * tt, struct flow * f, struct flow * mask, struct trigger ** temptable, uint16_t * num);
bool triggertable_getreport(struct triggertable * tt, struct trigger * t, char * buf, uint32_t time);
struct triggertype * triggertable_gettype(struct triggertable * tt, uint8_t type_id);

struct triggertype * triggertype_init(uint16_t id, trigger_update_func update_func, trigger_report_func report_func, trigger_apply_func	free_func, trigger_apply_func reset_func, trigger_apply_func print_func, uint16_t reset_interval, struct summary ** s, int summarynum, trigger_condition_func condition_func, uint32_t tickspersweep);
void triggertype_finish(struct triggertype * type);

void trigger_print2(struct trigger * t, void * aux);
void trigger_print(struct trigger * t, void * aux);
void trigger_cleantfl(struct trigger * t, struct triggertable * tt);
bool trigger_match(struct trigger * t, struct flow * f, struct flow * tempflow);

struct trigger * counter_trigger_init(struct trigger * t, uint16_t eventid, uint16_t id, struct flow * filter, struct flow * mask, struct triggertype * type, uint32_t threshold);
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

#endif /* triggertable2.h */
