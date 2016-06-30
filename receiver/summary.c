#include "summary.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "flatreport.h"
#include "util.h"
#include <rte_cycles.h>

#define LOSSBITMAP_SIZE (1<<20)
#define LOSSBITMAP_SIZE_MASK (LOSSBITMAP_SIZE-1)

void summary_fill0(struct summary *s, char * buf, struct flatreport * fr);
void summary_null(struct summary *s, char * buf, struct flatreport *  fr);
void summary_free(struct summary *s);
int16_t summary_getbufpos(struct summary *s, struct flowentry * fe, struct summary_table *st);

void summary_init(struct summary_table * st){
	st->filled = 0;
	memset(st->summaries, 0, sizeof(struct summary *) * SUMMARIES_NUM);
}

void summary_finish(struct summary_table * st){
	int i;
	for (i = 0; i < st->filled; i++){
		st->summaries[i]->free(st->summaries[i]);
	}
	st->filled = 0;
}

struct summary * summary_hassummary(struct summary_table * st, const char * name){
	//find the summary
	int i;
	for (i = 0; i < st->filled; i++){
		if (strncmp(st->summaries[i]->name, name, SUMMARY_NAME_LEN)==0){
			return st->summaries[i];
		}
	}
	return NULL;
}

struct summary * summary_add(struct summary_table * st, struct summary * s){
	if (st->filled >= SUMMARIES_NUM){
		fprintf(stderr, "No more bitmap space for summaries");
		return NULL;
	}
/*	THIS SHOULD BE VALUDATED PER FLOW
	if (st->filled >0 && st->summaries[st->filled-1]->buf_pos+st->summaries[st->filled-1]->size + 
			s->size > FLOWENTRY_BUF_SIZE){ 
		fprintf(stderr, "No more buffer space for summaries");
		return NULL;
	}*/
		
	st->summaries[st->filled] = s;	
	s->mask = (summarymask_t) (1<<st->filled);
	s->index = st->filled;
	st->filled++;
	return s;
}

/*inline void summary_apply2(struct summary_table * st, struct flowentry * fe, uint32_t offset, struct flatreport * fr){
        summarymask_t b;
        unsigned j;
        for (j = 0, b = 1; j < sizeof(summarymask_t)*8; j++, b <<= 1){
                if (fe->summaries & b){
			summary_apply_func func = *((summary_apply_func *)(((char *)st->summaries[j]) + offset));
                        func(st->summaries[j], fe->buf + st->summaries[j]->buf_pos, fr);
                }
        }
}*/

inline void summary_apply(struct summary_table * st, struct flowentry * fe, summary_apply_func func, struct flatreport * fr){
	summarymask_t b ;
	unsigned j;
	for (j = 0, b = fe->summaries; b > 0; j++, b >>= 1){
		if (b & 0x01){
			func(st->summaries[j], fe->buf + fe->summary_pos[j], fr);
		}
	}
}

inline void summary_apply_update(struct summary_table *st, struct flowentry * fe, struct flatreport * fr, struct flatreport_pkt * pkt){
	summarymask_t b;
        unsigned j;
        for (j = 0, b = fe->summaries; b > 0; j++, b >>= 1){
                if (b & 0x01){
                        st->summaries[j]->update(st->summaries[j], fe->buf + fe->summary_pos[j], fr, pkt);
                }
        }	
}

void summary_apply_finish(struct summary *s, char * buf, struct flatreport * fr){
	s->finish(s, buf, fr);
}

void summary_apply_reset(struct summary *s, char * buf, struct flatreport * fr){
	s->reset(s, buf, fr);
}

inline int16_t summary_getbufpos(struct summary *s, struct flowentry * fe, struct summary_table * st){
	if (!(fe->summaries & s->mask)){
                fprintf(stderr, "summary: flowentry doesn't have summary %s with mask %x\n", s->name, s->mask);
		return 0;
        }
	summarymask_t b;
        uint16_t buf_pos = 0;
        uint16_t j;
        for (b = 1, j = 0;  b < s->mask; b <<= 1, j++){
                if (b & fe->summaries){
                        buf_pos += st->summaries[j]->size;
                }
        }
	return buf_pos;
}

inline void summary_updatesummaries(struct summary_table * st, struct flowentry * fe){
        summarymask_t b;
        uint8_t buf_pos;
	int i;

        buf_pos = 0;
        for (i = 0, b = fe->summaries; b > 0; i++, b >>= 1){
                if (b & 0x01){
                        fe->summary_pos[i] = buf_pos;
                        buf_pos += st->summaries[i]->size;
                }
        }
}

void summary_fill0(struct summary *s, char * buf, struct flatreport * fr __attribute__((unused))){
	memset(buf, 0, s->size);
}

void summary_null(struct summary *s __attribute__((unused)), char * buf __attribute__((unused)), struct flatreport * fr __attribute__((unused))){
}

void summary_free(struct summary *s){
	FREE(s);
}

/*----------------------------counter ----------------------- */

void summary_counter_reset(struct summary *s, char * buf, struct flatreport * fr);
uint32_t summary_counter_get(char * buf, struct flatreport * fr);

inline uint32_t summary_counter_get(char * buf, struct flatreport * fr __attribute__((unused))){
        return *((uint32_t*)buf + 1);
}

void summary_counter_reset(struct summary *s __attribute__((unused)), char * buf, struct flatreport * fr __attribute__((unused))){
	uint32_t * v0 = (uint32_t*)buf;
	uint32_t * v1 = (uint32_t*)buf + 1;
	*v1 = *v0;
	*v0 = 0;
}


/*----------------------------pkt num ---------------------- */
void summary_pktnum_update(struct summary *s, char * buf, struct flatreport * fr, struct flatreport_pkt * pkt);

void summary_pktnum_update(struct summary *s __attribute__((unused)), char * buf, struct flatreport * fr __attribute__((unused)), struct flatreport_pkt * pkt __attribute__((unused))){
	uint32_t * v = (uint32_t*)buf;
	*v += 1;
}

inline uint32_t summary_pktnum_get(char * buf, struct flatreport * fr){
	return summary_counter_get(buf, fr);
}

struct summary * summary_pktnum_init(const char * name){
	struct summary * s = MALLOC(sizeof(struct summary));
	s->size = 8;
	s->update = summary_pktnum_update;
	s->finish = summary_null;
	s->reset = summary_counter_reset;
	s->moredata = NULL;
	s->free = summary_free;
	strncpy(s->name, name,SUMMARY_NAME_LEN-1);
	return s;
}

/*----------------------------volume ----------------------- */
void summary_volume_update(struct summary *s, char * buf, struct flatreport * fr, struct flatreport_pkt * pkt);

void summary_volume_update(struct summary *s __attribute__((unused)), char * buf, struct flatreport * fr __attribute__((unused)), struct flatreport_pkt * pkt){
	uint32_t * v = (uint32_t *)buf;
	*v += pkt->length;
//	LOG("v %p %p %d %d\n", buf, v, pkt->length, *v);
}

inline uint32_t summary_volume_get(char * buf, struct flatreport * fr){
	return summary_counter_get(buf, fr);
}

struct summary * summary_volume_init(const char * name){
	struct summary * s = MALLOC(sizeof(struct summary));
	s->size = 8;
	s->update = summary_volume_update;
	s->finish = summary_null;
	s->reset = summary_counter_reset;
	s->moredata = NULL;
	s->free = summary_free;
	strncpy(s->name, name, SUMMARY_NAME_LEN - 1);
	return s;
}

/*----------------------------lossnum2 --------------------- */
void summary_lossnum2_update(struct summary *s, char * buf, struct flatreport * fr, struct flatreport_pkt * pkt);

void summary_lossnum2_update(struct summary *s __attribute__((unused)), char * buf, struct flatreport * fr __attribute__((unused)), struct flatreport_pkt * pkt){
	uint32_t * seq = (uint32_t*)buf + 2;//keep two values there
	if (pkt->seq > *seq){
		*seq = pkt->seq;
	}else{
		uint32_t * v = (uint32_t*)buf;
                *v += 1;
	}
}

uint32_t summary_lossnum2_get(char * buf, struct flatreport * fr){
        return summary_counter_get(buf, fr);
}

struct summary * summary_lossnum2_init(const char * name){
        struct summary * s = MALLOC(sizeof(struct summary));
        s->size = 12;
        s->update = summary_lossnum2_update;
        s->finish = summary_null;
        s->reset = summary_counter_reset;
        s->free = summary_free;
        s->moredata = NULL;
        strncpy(s->name, name, SUMMARY_NAME_LEN-1);
        return s;
}

/*------------------------- ack & syn ---------------------- */
void summary_ack_update(struct summary *s, char * buf, struct flatreport * fr, struct flatreport_pkt * pkt);
void summary_ack_reset(struct summary *s, char * buf, struct flatreport * fr);
void summary_syn_update(struct summary *s, char * buf, struct flatreport * fr, struct flatreport_pkt * pkt);
void summary_syn_reset(struct summary *s, char * buf, struct flatreport * fr);

uint32_t summary_ack_get(char * buf){
        return *((uint32_t*)buf + 1);
}

uint32_t summary_ack_get2(char * buf){
        return *((uint32_t*)buf + 2);
}

uint32_t summary_ack_getdup(char * buf){
        return *((uint32_t*)buf + 4);
}


void summary_ack_reset(struct summary *s __attribute__((unused)), char * buf, struct flatreport * fr __attribute__((unused))){
	uint32_t * v0 = (uint32_t*)buf;
	uint32_t * v1 = (uint32_t*)buf + 1;
	uint32_t * v2 = (uint32_t*)buf + 2;
	*v2 = *v1;
	*v1 = *v0;
//	*v0 = 0;
	uint32_t * dup0 = (uint32_t*)buf + 3;
	uint32_t * dup1 = (uint32_t*)buf + 4;
	*dup1 = *dup0;
//	*dup0 = 0;
}
void summary_ack_update(struct summary *s __attribute__((unused)), char * buf, struct flatreport * fr __attribute__((unused)), struct flatreport_pkt * pkt){
	uint32_t * v0 = (uint32_t*)buf;
	uint32_t * dup0 = (uint32_t*)buf + 3;
	//ack is always increasing
	if (unlikely(*v0 == pkt->ack)){
		*dup0 = *dup0 + 1460;
	}else{
		if (*dup0 < pkt->ack - *v0){
			*dup0 = 0;
		}else{
			*dup0 -= pkt->ack - *v0;
		}
		*v0 = pkt->ack;
	}
//	printf("%p %"PRIu32",%"PRIu32",%"PRIu32"\n", buf, pkt->f.srcip, pkt->ack, pkt->seq); 
}

struct summary * summary_ack_init(const char * name){
	struct summary * s = MALLOC(sizeof(struct summary));
        s->size = 20;
        s->update = summary_ack_update;
        s->finish = summary_null;
        s->reset = summary_ack_reset;
        s->free = summary_free;
        s->moredata = NULL;
        strncpy(s->name, name, SUMMARY_NAME_LEN-1);
        return s;
}


uint32_t summary_syn_get(char * buf){
        return *((uint32_t*)buf + 1);
}

uint32_t summary_syn_get2(char * buf){
        return *((uint32_t*)buf + 2);
}

void summary_syn_reset(struct summary *s __attribute__((unused)), char * buf, struct flatreport * fr __attribute__((unused))){
	uint32_t * v0 = (uint32_t*)buf;
	uint32_t * v1 = (uint32_t*)buf + 1;
	uint32_t * v2 = (uint32_t*)buf + 2;
	*v2 = *v1;
	*v1 = *v0;
//	*v0 = 0;
}


void summary_syn_update(struct summary *s __attribute__((unused)), char * buf, struct flatreport * fr __attribute__((unused)), struct flatreport_pkt * pkt){
	uint32_t * v0 = (uint32_t*)buf;
	if (*v0 < pkt->seq){
		*v0 = pkt->seq;
	}
}

struct summary * summary_syn_init(const char * name){
	struct summary * s = MALLOC(sizeof(struct summary));
        s->size = 12;
        s->update = summary_syn_update;
        s->finish = summary_null;
        s->reset = summary_syn_reset;
        s->free = summary_free;
        s->moredata = NULL;
        strncpy(s->name, name, SUMMARY_NAME_LEN-1);
        return s;
}


/*----------------------------lossnum ---------------------- */
bool summary_lossfinder_updateandislost(struct lossfinder * lf, struct flatreport_pkt * pkt, struct flatreport * fr);

struct lossfinder * summary_lossfinder_init(void){
	struct lossfinder * lf = MALLOC(sizeof (struct lossfinder));

        lf->lossbm1 = bitmap_init(LOSSBITMAP_SIZE, NULL);
        lf->lossbm2 = bitmap_init(LOSSBITMAP_SIZE, NULL);

	lf->lasthash = 0;
	lf->lastresult = 2; //2 is for not set
	lf->needsreset = false;
	lf->pkts_num = 0;

	return lf;
}

void summary_lossfinder_finish(struct lossfinder * lf){
	bitmap_finish(lf->lossbm1);
	bitmap_finish(lf->lossbm2);
	FREE(lf);
}

void summary_lossfinder_reset(struct lossfinder * lf){
//	LOG("lossfinder: bitmap is full %d\n", bitmap_getfilled(lf->lossbm2));
	bitmap_clear(lf->lossbm1);
/*	uint32_t x = rte_rdtsc();
	while (rte_rdtsc()-x<6000*2.3){}*/
	struct bitmap * temp = lf->lossbm1;
	lf->lossbm1 = lf->lossbm2;
	lf->lossbm2 = temp;
}

bool summary_lossfinder_updateandislost(struct lossfinder * lf, struct flatreport_pkt * pkt, struct flatreport * fr __attribute__((unused))){
	uint32_t hash = (pkt->seq ^ (pkt->seq>>16)) & LOSSBITMAP_SIZE_MASK; //(pkt->seq ^ pkt->hash) & LOSSBITMAP_SIZE_MASK;
	lf->pkts_num++;
	if (!(lf->lasthash == hash && lf->lastresult == 1)){
		lf->lastresult = bitmap_set(lf->lossbm2, hash);
		if (!lf->lastresult){
			lf->lastresult |=  bitmap_get(lf->lossbm1, hash); //first set then get to make sure lossbm2 always has latest bits
		}
		lf->lasthash = hash;
//		if (bitmap_getfilled(lf->lossbm2) > (LOSSBITMAP_SIZE>>2)){
		if (lf->pkts_num > 1<<18){
			summary_lossfinder_reset(lf);
			lf->pkts_num = 0;
		}	
	}
	return lf->lastresult;
}

void summary_lossnum_update(struct summary *s, char * buf, struct flatreport * fr, struct flatreport_pkt * pkt);

void summary_lossnum_update(struct summary *s, char * buf, struct flatreport * fr, struct flatreport_pkt * pkt){
	if (summary_lossfinder_updateandislost((struct lossfinder *) s->moredata, pkt, fr)){
        	uint32_t * v = (uint32_t*)buf;
	        *v += 1;
	}
}

uint32_t summary_lossnum_get(char * buf, struct flatreport * fr){
	return summary_counter_get(buf, fr);
}

struct summary * summary_lossnum_init(struct lossfinder * lf, const char * name){
	struct summary * s = MALLOC(sizeof(struct summary));
	s->moredata = lf;
        s->size = 8;
        s->update = summary_lossnum_update;
        s->finish = summary_null;
        s->reset = summary_counter_reset;
        s->free = summary_free;
        strncpy(s->name, name, SUMMARY_NAME_LEN-1);
        return s;
}


/*----------------------------losslist --------------------- */
void summary_loss_finish(struct summary *s, char * buf, struct flatreport * fr);
void summary_loss_update(struct summary *s, char * buf, struct flatreport * fr, struct flatreport_pkt * pkt);
void summary_loss_reset(struct summary *s, char * buf, struct flatreport * fr);

void losslist_print(uint32_t seq, void * aux __attribute__((unused))){
	printf("%lu, ", (long unsigned)seq);
}

void losslisthead_apply(struct losslisthead * llh, loss_apply_func loss_func, void * aux){
	int i;
	struct losslist * ll =  llh->first_ll;	
	if (ll == NULL){
		return;
	}
	while (ll->next != NULL){
		for (i = 0; i < LOSSLIST_BLOCK; i++){
			loss_func(ll->lost[i], aux);
		}
		ll = ll->next;
	}
	
	for (i = 0; i < llh->nextIndex; i++){
		loss_func(ll->lost[i], aux);
	}
}

struct losslist * losslist_init(void){
	struct losslist * ll = (struct losslist *) MALLOC(sizeof(struct losslist));
	//memset(ll, 0, LOSSLIST_BLOCK * sizeof(uint32_t));
	ll->next = NULL;
	return ll;
}

void losslist_finish(struct losslist * ll){
	if (ll->next != NULL){
		losslist_finish(ll->next);
	}
	FREE(ll);
}

inline struct losslisthead * summary_losslist_get(char * buf){
	return (struct losslisthead *)(buf);
}

void summary_loss_finish(struct summary *s __attribute__((unused)), char * buf, struct flatreport * fr __attribute__((unused))){
	struct losslisthead * llh = (struct losslisthead *)buf;
	if (llh->first_ll == NULL){
		return;
	}
	losslist_finish(llh->first_ll);
	llh->first_ll = NULL;
	llh->last_ll = llh->first_ll;
	llh->nextIndex = 0;
}

void summary_loss_update(struct summary *s __attribute__((unused)), char * buf, struct flatreport * fr __attribute__((unused)), struct flatreport_pkt * pkt){
//	if (!summary_lossfinder_updateandislost((struct lossfinder *)s->moredata, pkt, fr)){
//		return;
//	}
	struct losslisthead * llh = (struct losslisthead *)buf;
        if (pkt->seq > llh->lastseq){
                llh->lastseq = pkt->seq;
		return;
        }
	if (llh->first_ll == NULL){
		llh->first_ll = losslist_init();
		llh->last_ll = llh->first_ll;
	}
	llh->last_ll->lost[llh->nextIndex] = pkt->seq;
	if (llh->nextIndex + 1 >= LOSSLIST_BLOCK){
		llh->nextIndex = 0;
		llh->last_ll->next = losslist_init();
		llh->last_ll = llh->last_ll->next;
	}else{
		llh->nextIndex++;
	}
}

//reset the counters but keep the first one unless it is empty
void summary_loss_reset(struct summary *s __attribute__((unused)), char * buf, struct flatreport * fr __attribute__((unused))){
	struct losslisthead * llh = (struct losslisthead *)buf;
	if (llh->first_ll == NULL){return;}
	if (llh->nextIndex == 0 && llh->first_ll->next == NULL){//even remove first
		losslist_finish(llh->first_ll);
		llh->first_ll = NULL;
	}else if (llh->first_ll->next != NULL){//keep first
		losslist_finish(llh->first_ll->next);
		llh->first_ll->next = NULL;
	}
	llh->last_ll = llh->first_ll;
	llh->nextIndex = 0;	
}

struct summary * summary_losslisthead_init(void){
	struct summary * s = MALLOC(sizeof(struct summary));
	s->size = sizeof(struct losslisthead);
	s->update = summary_loss_update;
	s->finish = summary_loss_finish;
	s->reset = summary_loss_reset;
	s->moredata = NULL;
	s->free = summary_free;
	snprintf(s->name,SUMMARY_NAME_LEN-1, "losslist");
	return s;
}

/*---------------------------- burstlist --------------------- */
void summary_burst_finish(struct summary *s, char * buf, struct flatreport * fr);
void summary_burst_update(struct summary *s, char * buf, struct flatreport * fr, struct flatreport_pkt * pkt);
bool burstlisthead_isburst2(struct burstlisthead * blh, uint32_t seq);

void summary_burst_finish(struct summary *s __attribute__((unused)), char * buf, struct flatreport * fr __attribute__((unused))){
	struct burstlisthead * blh = (struct burstlisthead *)buf;
	if (blh->first_bl != NULL){
		burstlist_finish(blh->first_bl);
		blh->first_bl = NULL;
		blh->last_bl = blh->first_bl;
		blh->nextIndex = 0;
	}
}

void summary_burst_update(struct summary *s __attribute__((unused)), char * buf, struct flatreport * fr, struct flatreport_pkt * pkt){
	uint64_t ts = fr->epoch_ts + pkt->ts;
	uint32_t seq = pkt->seq;
	struct burstlisthead * blh = (struct burstlisthead *)buf;
	if (ts - blh->ts < FLOWENTRY_BURST_TS_THRESHOLD){ //blh->ts is by default zero
		blh->num++;
		if (blh->num == 1){
			blh->seq1 = seq;
			blh->seq2 = seq;
		}else{
			blh->seq1 = blh->seq1<seq ? blh->seq1 : seq;
			blh->seq2 = blh->seq2>seq ? blh->seq2 : seq;
		}
	}else{		
		burstlisthead_dump(blh);
		blh->num = 1;
		blh->seq1 = seq;
		blh->seq2 = seq;
	}

	blh->ts = ts;
}

inline struct burstlisthead * summary_burstlist_get(char * buf){
	return (struct burstlisthead *)(buf);
}

void burstlisthead_dump(struct burstlisthead * blh){
		if (blh->num <= FLOWENTRY_BURST_NUM_THRESHOLD){ //dump it to burstlists
			return;
		}
		if (blh->first_bl == NULL){
			blh->first_bl = burstlist_init();
			blh->last_bl = blh->first_bl;
		}
		struct burst * b = &blh->last_bl->b[blh->nextIndex];
		b->seq1 = blh->seq1;
		b->seq2 = blh->seq2;
		if (blh->nextIndex + 1 >= BURSTLIST_BLOCK){
			blh->nextIndex = 0;
			//make it a circual buffer by commenting the following. TODO: can remove last_bl pointer
			/*blh->last_bl->next = burstlist_init();
			blh->last_bl->next->prev = blh->last_bl;
			blh->last_bl = blh->last_bl->next;*/
		}else{
			blh->nextIndex++;
		}
}

void burstlist_print(struct burst * b, void * aux __attribute__((unused))){
	printf("%lu,%lu ", (long unsigned)b->seq1, (long unsigned)b->seq2);
}

// assume asked in a sorted fashion and bursts are sorted too!
bool burstlisthead_isburst(struct burstlisthead * blh, uint32_t seq, void * state1){
	if (blh->first_bl == NULL){
		return false;
	}
	struct burstsearchstate * state = (struct burstsearchstate * )state1;
	if (state->current == NULL){
		state->current = blh->first_bl;
		state->nextIndex = 0;
	}
	uint16_t nextIndex = state->nextIndex;
	struct burstlist * current = state->current;
	while (current->next != NULL || nextIndex < blh->nextIndex){
		if  (current->b[nextIndex].seq1 <= seq && seq <= current->b[nextIndex].seq2){
			state->current = current;
			state->nextIndex = nextIndex;
			return true;
		}else if (current->b[nextIndex].seq2 < seq){ //then it is worth going forward
			if (nextIndex + 1 >= BURSTLIST_BLOCK && current->next != NULL){ //next list
				current = current->next;
				nextIndex = 0;
			}else{
				nextIndex++;
			}
		}else{
			state->current = current;
			state->nextIndex = nextIndex;
			return false;
		}
	}
	return false;
}

// assume asked in a sorted fashion and bursts are sorted too!
bool burstlisthead_isburst2(struct burstlisthead * blh, uint32_t seq){
	if (blh->first_bl == NULL){
		return false;
	}
	uint16_t nextIndex = 0;
	struct burstlist * current = blh->first_bl;
	while (current->next != NULL || nextIndex < blh->nextIndex){
		if  (current->b[nextIndex].seq1 <= seq && seq <= current->b[nextIndex].seq2){
			return true;
		}else{
			if (nextIndex + 1 >= BURSTLIST_BLOCK && current->next != NULL){ //next list
				current = current->next;
				nextIndex = 0;
			}else{
				nextIndex++;
			}
		}
	}
	return false;
}

void burstlisthead_apply(struct burstlisthead * blh, burst_apply_func burst_func, void * aux){
	int i;
	struct burstlist * bl =  blh->first_bl;	
	if (bl == NULL){
		return;
	}
	while (bl->next != NULL){
		for (i = 0; i < BURSTLIST_BLOCK; i++){
			burst_func(&bl->b[i], aux);
		}
		bl = bl->next;
	}
	
	for (i = 0; i < blh->nextIndex; i++){
		burst_func(&bl->b[i], aux);
	}
}

//reset the counters but keep the first one unless it is empty
void burstlisthead_reset(struct burstlisthead * blh){
	if (blh->first_bl != NULL){
		if (blh->nextIndex == 0 && blh->first_bl->next == NULL){//even remove first
			burstlist_finish(blh->first_bl);
			blh->first_bl = NULL;
		}else if (blh->first_bl->next != NULL){//keep first
			burstlist_finish(blh->first_bl->next);
			blh->first_bl->next = NULL;
		}
		blh->last_bl = blh->first_bl;
		blh->nextIndex = 0;
	}
	blh->ts = 0;
	blh->num = 0;
	blh->seq1 = 0;
	blh->seq2 = 0;
}

struct burstlist * burstlist_init(void){
	struct burstlist * bl = (struct burstlist * )MALLOC(sizeof(struct burstlist));
	//memset(bl, 0, burstlist_BLOCK * sizeof(uint32_t));
	bl->next = NULL;
	bl->prev = NULL;
	return bl;
}

void burstlist_finish(struct burstlist * bl){
	if (bl->next != NULL){
		burstlist_finish(bl->next);
	}
	FREE(bl);
}

struct summary * summary_burstlisthead_init(void){
	struct summary * s = MALLOC(sizeof(struct summary));
	s->size = sizeof(struct burstlisthead);
	s->update = summary_burst_update;
	s->finish = summary_burst_finish;
	s->reset = summary_null;
	s->moredata = NULL;
	s->free = summary_free;
	snprintf(s->name,SUMMARY_NAME_LEN-1, "burst");
	return s;
}
