#ifndef SUMMARY_H
#define SUMMARY_H 1
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include <stddef.h> //for offsetof
#include "flowentry.h"
#include "bitmap.h"

#define SUMMARY_NAME_LEN 16

#define SUMMARY_UPDATE_FUNC_OFFSET offsetof(struct summary, update)

struct flatreport;
struct flatreport_pkt;
struct summary;

typedef void (*summary_apply_func)(struct summary *s, char * buf, struct flatreport * fr);
typedef void (*summary_apply_update_func)(struct summary *s, char * buf, struct flatreport * fr, struct flatreport_pkt * pkt);
typedef void (*summary_apply_func2)(struct summary *s);

struct summary{
	uint16_t size; //number of bytes to be reserved in the flowtable per flow
	uint8_t index;
	summarymask_t mask;
	summary_apply_update_func update;
	summary_apply_func reset;
	summary_apply_func finish; //must free any resources malloced
	summary_apply_func2 free; //just in case somebody need to instantiate the summary out of the flowtable
	void * moredata; //points to subclasses
	char name[SUMMARY_NAME_LEN];
};

struct summary_table{
	struct summary * summaries[SUMMARIES_NUM];
	uint8_t filled;
};

void summary_init(struct summary_table * st);
void summary_finish(struct summary_table * st);
struct summary * summary_hassummary(struct summary_table * st, const char * name);
struct summary * summary_add(struct summary_table * st, struct summary * s);

/*
* applies the function on all the summaries enabled for the flowentry fe
*/
void summary_apply(struct summary_table * st, struct flowentry * fe, summary_apply_func func, struct flatreport * fr);

/*
* calls the update function for all summaries on the flowentry
*/
void summary_apply_update(struct summary_table * st, struct flowentry * fe, struct flatreport * fr, struct flatreport_pkt * pkt);

/*
* calls the finish function for the summary that uses the buffer buf
*/
void summary_apply_finish(struct summary *s, char * buf, struct flatreport * fr);

/*
* calls the reset function for the summary as if it uses the buffer buf
*/
void summary_apply_reset(struct summary *s, char * buf, struct flatreport * fr);

/*----------------------------------- pktnum summary -----------------------*/
uint32_t summary_pktnum_get(char * buf, struct flatreport * fr);
struct summary * summary_pktnum_init(const char * name);

/*----------------------------------- volume summary -----------------------*/
uint32_t summary_volume_get(char * buf, struct flatreport * fr);
struct summary * summary_volume_init(const char * name);

/*----------------------- -- # retransmitted packets summary ---------------*/
uint32_t summary_lossnum2_get(char * buf, struct flatreport * fr);
struct summary * summary_lossnum2_init(const char * name);

/*----------------------------------- # acks -------------------------------*/
uint32_t summary_ack_get(char * buf);
uint32_t summary_ack_get2(char * buf);
uint32_t summary_ack_getdup(char * buf);
struct summary * summary_ack_init(const char * name);


/*------------------------------- maximum syn number------------------------*/
uint32_t summary_syn_get(char * buf);
uint32_t summary_syn_get2(char * buf);
struct summary * summary_syn_init(const char * name);


/*---------------------------- loss num using a bitmap----------------------*/

/*
* A shared data structure among all lossnum summaries that keeps track of
* seen sequence numbers in a bitmap
*/
struct lossfinder{
        struct bitmap * lossbm1;
        struct bitmap * lossbm2;
        uint32_t lasthash;
	uint32_t pkts_num;
        uint8_t lastresult;
	uint8_t needsreset;
};

struct lossfinder * summary_lossfinder_init(void);
void summary_lossfinder_finish(struct lossfinder * lf);
void summary_lossfinder_reset(struct lossfinder * lf);

uint32_t summary_lossnum_get(char* buf, struct flatreport * fr);
struct summary * summary_lossnum_init(struct lossfinder * lf, const char * name);


/*------------ list of sequence number of retransmitted packets---------------*/
struct losslisthead{
	struct losslist * last_ll;
	struct losslist * first_ll;
	uint32_t lastseq;
	uint16_t nextIndex;
};

struct losslist {
	uint32_t lost[LOSSLIST_BLOCK];
	struct losslist * next;
};

typedef void (*loss_apply_func)(uint32_t seq, void * aux);

void losslisthead_add(struct losslisthead * llh, uint32_t seq);
void losslisthead_apply(struct losslisthead * llh, loss_apply_func, void * aux);
void losslisthead_reset(struct losslisthead * llh);
struct summary * summary_losslisthead_init(void);
struct losslisthead * summary_losslist_get(char* buf);

struct losslist * losslist_init(void);
void losslist_finish(struct losslist * ll);
void losslist_print(uint32_t seq, void * aux);


/*-------------------------------- burst detection --------------------------*/
/*
* start and finish sequence numbers of a burst
*/
struct burst{
	uint32_t seq1;
	uint32_t seq2;
};

/*
* a block of bursts
*/
struct burstlist{
	struct burst b[BURSTLIST_BLOCK];
	struct burstlist * next;
	struct burstlist * prev;
};

/*
* burst summary
*/
struct burstlisthead{
	uint64_t ts;
	uint32_t seq1;
	uint32_t seq2;
	uint16_t num;
	uint16_t nextIndex;
	struct burstlist * last_bl;
	struct burstlist * first_bl;
};

struct burstsearchstate{
	struct burstlist * current;
	uint16_t nextIndex;
};

typedef void (*burst_apply_func)(struct burst * b, void * aux);

void burstlisthead_add(struct burstlisthead * blh, uint32_t seq, uint64_t ts);
void burstlisthead_apply(struct burstlisthead * blh, burst_apply_func, void * aux);
bool burstlisthead_isburst(struct burstlisthead * blh, uint32_t seq, void * state1);
struct burstlisthead * summary_burstlist_get(char * buf);
void burstlisthead_reset(struct burstlisthead * blh);
void burstlisthead_dump(struct burstlisthead * blh);
struct summary * summary_burstlisthead_init(void);
void summary_updatesummaries(struct summary_table * st, struct flowentry * fe);

struct burstlist * burstlist_init(void);
void burstlist_finish(struct burstlist * bl);
void burstlist_print(struct burst * b, void * aux);

#endif /* summary.h */
