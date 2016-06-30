#ifndef FLOWENTRY_H
#define FLOWENTRY_H 1
#include <stdint.h>
#include "flow.h"
#include "hashmap.h"
#include "stdbool.h"
#include "util.h"

#define FLOWENTRY_TRIGGER_SIZE 16
#define FLOWENTRY_BURST_SIZE 16
#define FLOWENTRY_BURST_TS_THRESHOLD 5000
#define FLOWENTRY_BURST_NUM_THRESHOLD 4
#define LOSSLIST_BLOCK 32
#define BURSTLIST_BLOCK 32

#if TRIGGERTABLE_SWEEP
#define FLOWENTRY_BUF_SIZE 91 // 128 - 12 - 8 - 8 - 1 - 8
#else
#define FLOWENTRY_BUF_SIZE 131 // 192 - 12 - 8 - 1 - 8 - 16*2
#endif

struct trigger;
typedef uint16_t flowkey_t;

typedef uint8_t summarymask_t;
#define SUMMARIES_NUM 8 //note size of give byte size not bit size

struct flowentry{
	struct flow f;
	hashmap_elem e; //make sure e is in the first cache line
#if TRIGGERTABLE_SWEEP
	uint32_t lastupdate;
	uint32_t lastreset;
	summarymask_t summaries;
	uint8_t summary_pos[SUMMARIES_NUM];
	char buf[FLOWENTRY_BUF_SIZE]; 
#else
	summarymask_t summaries;
	uint8_t summary_pos[SUMMARIES_NUM];
	char buf[FLOWENTRY_BUF_SIZE]; //putting this before triggers reduces cache miss alot
	uint16_t triggers[FLOWENTRY_TRIGGER_SIZE]; 
#endif
};

struct flowentry * flowentry_init(void);
void flowentry_finish (struct flowentry * fe);
//void flowentry_finish2(struct flowentry * fe);
bool flowentry_equal(void * data1, void * data2, void * aux);
bool flowentry_print2(uint16_t id, void * data, void * aux);
void flowentry_unset_cachedtrigger(struct flowentry * fe);
bool flowflowentry_equal(void * newdata, void * data2, void * aux);
void flowflowentry_init(void * newdata, void * data2, void * aux);
bool flowentry_isobsolete(struct flowentry * fe, uint32_t step);

#endif /* flowentry.h */
