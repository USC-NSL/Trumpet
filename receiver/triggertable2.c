#include "triggertable2.h"
#include <stdio.h>
#include <stdlib.h> 
#include <stdint.h>
#include <string.h> 
#include <stddef.h> //for offsetof
#include "util.h"
#include "flatreport.h"
#include "summary.h"

#define TRIGGERTABLE_FLOWLIST_POOLSIZE  (1<<21)
#define TRIGGERTABLE_TYPE_FGID 0xFF

#if TRIGGERTABLE_INLINE_TRIGGER
#define getTrigger(p,i) p + i;
#else 
#define getTrigger(p,i) p[i];
#endif

int acted = 1;
static int x;

void trigger_finish(struct trigger * t, struct triggertable * tt);
void triggertable_addviolatedtrigger(struct triggertable * tt, struct trigger * t);

bool triggerpointer_equal (void * data1, void * data2, void * aux);
bool trigger_equal (void * data1, void * data2);
void trigger_finish2(struct trigger * data, void * aux);
struct table * triggertable_findtable(struct triggertable * tt, struct trigger * t);
bool triggertype_equal(struct triggertype * type1, struct triggertype * type2);
void triggertype_print(struct triggertype * type);
void trigger_init(uint16_t eventid,  struct trigger * t, struct flow * filter, struct flow * mask, struct triggertype * type);
void counter_trigger_update(struct trigger * t, uint32_t value, struct triggertable * tt);
void burstloss_check(uint32_t seq, void * aux);
void triggertable_finishsweep(struct triggertable * tt);
uint16_t readtfl(struct triggertable * tt, struct trigger * t);
void updatetickspertype(struct triggertable * tt, uint32_t sweeptime, uint32_t totalflow);
void triggertable_newtriggerflowlistpool(struct triggertable * tt);
struct triggerflowlist * getnewflowlist(struct triggertable * tt);
uint16_t trigger_getpos(struct triggertable * tt, struct trigger *t);

bool trigger_isfgmaster(struct trigger * t);
struct flow * trigger_fgmask(struct trigger * t);
struct trigger * trigger_fginit(struct trigger * t, struct trigger * t2, struct flow * f);

struct triggertable * triggertable_init(struct flatreport * fr){
	x=0;
	struct triggertable * tt = MALLOC (sizeof (struct triggertable));
	tt->fr = fr;
	tt->violated_entry_index = 0;
	tt->types = NULL;
	tt->m = matcher_init();
	tt->violated_entries_buffer = (struct trigger **) MALLOC (sizeof(void *)*VIOLATED_ENTRIES_SIZE);
	memset(tt->violated_entries_buffer, 0, sizeof(void *)*VIOLATED_ENTRIES_SIZE);
	tt->filled = 0;

	tt->trigger_pos_bm = bitmap_init(TRIGGERTABLE_POSITION_TABLE_SIZE, NULL);
	bitmap_setall(tt->trigger_pos_bm);
	bitmap_unset(tt->trigger_pos_bm, 0); //don't use 0
#if TRIGGERTABLE_INLINE_TRIGGER
	tt->position_table = BIGMALLOC2(sizeof(struct trigger) * TRIGGERTABLE_POSITION_TABLE_SIZE, 64);
	memset(tt->position_table, 0, sizeof(struct trigger) * TRIGGERTABLE_POSITION_TABLE_SIZE);
#else
	tt->position_table = MALLOC(sizeof(struct trigger *) * TRIGGERTABLE_POSITION_TABLE_SIZE);
	memset(tt->position_table, 0, sizeof(struct trigger *) * TRIGGERTABLE_POSITION_TABLE_SIZE);
#endif

	tt->freelist = NULL;
	tt->pools = NULL;

#if TRIGGERTABLE_SWEEP
	int i;
	for (i = 0; i < 20; i++){
		triggertable_newtriggerflowlistpool(tt);
	}
#endif

	tt->lastid = 0;
	tt->fgtype =  triggertype_init(TRIGGERTABLE_TYPE_FGID, fgcounter_trigger_update, fgcounter_trigger_report, counter_trigger_free, fgcounter_trigger_reset, fgcounter_trigger_print, NULL, 0, fgcounter_trigger_condition, 30);
	triggertable_addtype(tt, tt->fgtype);
	return tt;
}

void triggertable_newtriggerflowlistpool(struct triggertable * tt){
	struct triggerflowlist * tfl2;
	struct triggerflowlist * tfl = BIGMALLOC2(TRIGGERTABLE_FLOWLIST_POOLSIZE, TRIGGERTABLE_FLOWLIST_POOLSIZE);
	uint32_t i;
	for (i = 0, tfl2 = tfl; i < TRIGGERTABLE_FLOWLIST_POOLSIZE/sizeof(struct triggerflowlist); i++, tfl2++){
		tfl2->next = tfl2 + 1;
	}
	tfl2--;
	tfl2->next = tt->freelist;
	tt->freelist = tfl;
	struct triggerflowlistpool * tflp = MALLOC (sizeof (struct triggerflowlistpool));
	tflp->tfl = tfl;
	tflp->next = tt->pools;
	tt->pools = tflp;
	//printf("got a new pool at %p\n", tflp->tfl);
}

void triggertable_finish(struct triggertable * tt){
	LOG("swept flows %d\n", x);
	matcher_finish(tt->m);
	triggertable_applyontriggers(tt, trigger_finish2, tt);
	tt->filled = 0;
	
	struct triggertype * type;
	struct triggertype * type2;
	for (type = tt->types; type != NULL; type = type2){
		type2 = type->next;
		triggertype_finish(type);
	}
	FREE(tt->violated_entries_buffer);
	bitmap_finish(tt->trigger_pos_bm);
#if TRIGGERTABLE_INLINE_TRIGGER
	BIGFREE(tt->position_table);
#else
	FREE(tt->position_table);
#endif

	struct triggerflowlistpool * tflp;
	for (;tt->pools != NULL; tt->pools = tflp){
		tflp = tt->pools->next;
		BIGFREE(tt->pools->tfl);
		FREE(tt->pools);
	}

	FREE(tt);
}

void triggertable_addtype(struct triggertable * tt, struct triggertype * type1){
	type1->next= NULL;
	if (tt->types == NULL){
		tt->types = type1;
	}else{
		struct triggertype * type = tt->types;		
		while (type->next != NULL){
			type = type->next;
		}
		if (type1==type){
			fprintf(stderr, "loop in trigger type\n");
		}
		type->next = type1;
	}
}


struct triggertype * triggertable_gettype(struct triggertable * tt, uint8_t type_id){
	struct triggertype * type;
	for (type = tt->types; type != NULL; type = type->next){
		if (type->id == type_id){
			return type;
		}
	}
	return NULL;
}

uint16_t triggertable_gettypenum(struct triggertable * tt){
	struct triggertype * type;
	uint16_t num = 0;
	for (type = tt->types; type != NULL; type = type->next){
		num++;
	}
	return num;
}


struct trigger * triggertable_gettrigger(struct triggertable * tt){
	struct bitmap_iterator bi;
	bitmap_iterator_init(tt->trigger_pos_bm, &bi);	
	uint32_t id;
	if (bitmap_iterator_next(&bi, &id)){
		struct trigger * t;
#if TRIGGERTABLE_INLINE_TRIGGER		 
		t = tt->position_table + id;
#else
		t = MALLOC(sizeof(struct trigger));
		tt->position_table[id] = t;
		t->pos = (uint16_t) id;
#endif
		bitmap_unset(tt->trigger_pos_bm, id);
		tt->filled++;
		t->id = tt->lastid++;
		return t;
	}else{
		fprintf(stderr, "triggertable: no more space to add a trigger\n");
		return NULL;
	}
}


bool triggertable_addtrigger(struct triggertable * tt, struct trigger * t){
#if TRIGGERTABLE_SWEEP
	if (t->tfl == NULL){
		struct triggerflowlist * tfl = getnewflowlist(tt);
		tfl->next = t->tfl;
		tfl->fullmap = 0;
		t->tfl = tfl;
                t->tfhead_filled = 0;
	}
#endif
	return matcher_add(tt->m, &t->filter, &t->mask, t);
}

// note that this will free the memory of trigger in the table. 
// thus t cannot be the same entry
bool triggertable_removetrigger(struct triggertable * tt, struct trigger * t){
	void * t2;
	if (matcher_remove(tt->m, &t->filter, &t->mask, t, trigger_equal, &t2)){
		tt->filled --;
		uint16_t pos = trigger_getpos(tt, (struct trigger *)t2);
		if (unlikely(bitmap_set(tt->trigger_pos_bm, pos))){
                      fprintf(stderr, "triggertable: wanted to remove trigger with pos %u but it wasn't there", pos);
			return false;
		}else{
#if TRIGGERTABLE_INLINE_TRIGGER
#endif
		}
		trigger_finish((struct trigger *)t2, tt);
		return true;
	}else{
		fprintf(stderr, "Trigger not found to be removed");
		return false;
	}	
}

void triggertable_applyontriggers(struct triggertable * tt, trigger_apply_func func, void * aux){
	uint32_t seen;
	uint32_t i;
	struct trigger * t;
	for (seen = 0, i = 0; seen < tt->filled && i < TRIGGERTABLE_POSITION_TABLE_SIZE; i++){ //this could also use bitmap_iterator
		t = getTrigger(tt->position_table, i);
		if (t == NULL || t->type == NULL){
			continue;
		}
		func(t, aux);
		seen++;
	}
}

void triggertable_naivesweep(struct triggertable * tt){
	uint16_t seen;
	uint16_t index;
	struct trigger * t;
	struct triggertype * type;
	for (seen = 0, index = 0; seen < tt->filled && index < TRIGGERTABLE_POSITION_TABLE_SIZE; index++){ //this could also use bitmap_iterator
	        t = getTrigger(tt->position_table, index);
                if (t == NULL || t->type == NULL){
                        continue;
                }
                type = t->type;
		seen ++;
		//set for reporting if necessary
                if (tt->fr->step - t->lastreset >= t->reset_interval){
                        if (type->condition_func(t)){
				flatreport_report(tt->fr, t);
                        }
                        type->reset_func(t, tt);
                        t->lastreset = tt->fr->step;
                }
	}
}

struct triggerflowlist * getnewflowlist(struct triggertable * tt){
	if (tt->freelist == NULL){
		triggertable_newtriggerflowlistpool(tt);
        }
        struct triggerflowlist * tfl = tt->freelist;
        tt->freelist = tt->freelist->next;
	return tfl;
}


void triggertable_justmatch(struct triggertable * tt, struct flow * f, struct flow * mask, struct trigger ** temptable, uint16_t * num){
	matcher_matchmask(tt->m, f, mask, (void **)temptable, num);
}

#if TRIGGERTABLE_SWEEP

void triggertable_match(struct triggertable * tt __attribute__((unused)), struct flowentry * fe __attribute__((unused)), struct summary_table * st __attribute__((unused))){}
void triggertable_update(struct triggertable * tt __attribute__((unused)), struct flowentry * fe __attribute__((unused)), struct summary_table *st __attribute__((unused))){}
void triggertable_update2(struct triggertable * tt __attribute__((unused)), struct flowentry * fe __attribute__((unused)), struct flatreport_pkt * pkt __attribute__((unused))){}
void triggertable_report(struct triggertable * tt __attribute__((unused))){}
void triggertable_addviolatedtrigger(struct triggertable * tt __attribute__((unused)), struct trigger * t __attribute__((unused))){}

uint16_t readtfl(struct triggertable * tt, struct trigger * t){
	uint64_t b, j;
	struct sweep_state * state = &tt->state; //not that state->tfl may be udpated later
	struct triggerflow * tf;
	uint16_t flow_micronum = 0;
	struct flatreport * fr = tt->fr;

/*	for (tf = state->tfl->tf, b = state->tfl->fullmap; b > 0; tf++, b >>= 1){
		if (b &0x01){
			SWEEP_PREFETCH0(tf->fe);
		}
	}*/
	struct flowentry * fe;
	
	for (tf = state->tfl->tf, j = 1, b = state->tfl->fullmap; b > 0; tf++, j<<=1, b >>= 1){
/*		if (b & 0x02){
			SWEEP_PREFETCH0((tf + 1)->fe);
		}*/
		if (b & 0x01){
			//get fe
//			if (unlikely(!flow_equal(&tf->fe->f, &tf->f))){
				//tf->fe = (struct flowentry *) hashmap_get2(fr->ft1, &tf->f, tf->hash, flowflowentry_equal, fr);
				fe = (struct flowentry *) hashmap_get2(fr->ft1, &tf->f, flow_hash(&tf->f), flowflowentry_equal, fr);
				if (fe == NULL){ //it could have been replaced or removed by other triggers
					state->tfl->fullmap &= ~j;//igonres this in timing stats
					continue;
				}			
//			}
			if (flowentry_isobsolete(fe, fr->step)){
				flatreport_flowentry_finish(fe, fr);
	        	        hashmap_remove(fr->ft1, fe);
				state->tfl->fullmap &= ~j;
				continue;
			}else{
				if (fe->lastreset < fr->step){ //the flow may not have any packet thus I reset it myself
					fe->lastreset = fr->step;
					summary_apply(fr->st, fe, summary_apply_reset, fr);	
				}
//				if (fe->lastupdate >= fr->step - 1){ //lets keep this separate from isobsolete, this optimization is agains some congestion triggers
				t->type->update_func(t, fe, tt);
//				}
			}
			flow_micronum++;
		}
	}
	if (unlikely(state->tfl->fullmap == 0)){
		//remove this list
		if (state->tfl_last == NULL){//this is head
			t->tfl = state->tfl->next;
			if (t->tfl != NULL && t->tfl->fullmap > 0){
				t->tfhead_filled = log2_64(t->tfl->fullmap) + 1;
			}else{
				t->tfhead_filled = 0;
			}
		}else{
			state->tfl_last->next = state->tfl->next;
		}
		struct triggerflowlist * temp = state->tfl->next;
		state->tfl->next = tt->freelist;
		tt->freelist = state->tfl;
		state->tfl = temp;
	}else{
		state->tfl_last = state->tfl;
		state->tfl = state->tfl->next;
	}
	return flow_micronum;
}

void updatetickspertype(struct triggertable * tt, uint32_t sweeptime, uint32_t totalflow){
	struct triggertype * type;
	for (type = tt->types; type != NULL; type = type->next){
		if (type->flow_micronum > 0){
			type->ticksperupdate2 += type->flow_micronum * sweeptime / totalflow;
			type->ticksperupdate_num += type->flow_micronum;
			type->flow_micronum = 0;
		}	
	}
}

bool triggertable_sweep(struct triggertable * tt, uint32_t sweeptime, const uint32_t minsweepticks){
	uint16_t flow_micronum, flow_num;
	struct trigger * t;
	struct flatreport * fr = tt->fr;
	uint64_t finish_tsc, call_tsc;
	int32_t ticksbudget = minsweepticks;
	struct sweep_state * state = &tt->state;
	struct triggertype * type;
	

	call_tsc = rte_rdtsc();
	flow_num = 0;
//	printf("start %d %d %d %d\n", tt->fr->step, state->seen, tt->filled, state->index);
	for (; state->seen < tt->filled && state->index < TRIGGERTABLE_POSITION_TABLE_SIZE; state->index++){ //this could also use bitmap_iterator
		t = getTrigger(tt->position_table, state->index);
		if (t == NULL || t->type == NULL){
			continue;
		}
		type = t->type;

		if (state->triggerinterrupted){
			state->triggerinterrupted = false;
		}else{
			state->tfl_last = NULL;
			state->tfl = t->tfl;	
		}
				
		while (state->tfl != NULL){
			flow_micronum = readtfl(tt, t);
			if (MULTISTEP){
				flow_num += flow_micronum;
				type->flow_micronum += flow_micronum;
				ticksbudget -= flow_micronum * type->ticksperupdate;
				if (ticksbudget <= 0){
					finish_tsc = rte_rdtsc();
					if (finish_tsc - call_tsc >= sweeptime){
						updatetickspertype(tt, finish_tsc - call_tsc, flow_num);
						state->triggerinterrupted = true;
						return false;
					}else{
						ticksbudget = minsweepticks;
					}
				}
			}
		}
		//set for reporting if necessary
		if (fr->step - t->lastreset >= t->reset_interval){
			if (type->condition_func(t)){
				flatreport_report(tt->fr, t);
			}
			type->reset_func(t, tt);
			t->lastreset = fr->step;
		}

		state->seen++;  //must be after interruption, don't put in for declaration

	}	
	finish_tsc = rte_rdtsc();
//	printf("finish took %"PRIu64" %d\n", finiosh_tsc - call_tsc, fr->step);

#if MULTISTEP
	updatetickspertype(tt, finish_tsc - call_tsc, flow_num);
#endif
	
	if (state->seen < tt->filled){
		fprintf(stderr, "TriggerTable sweep: seen < filled \n");
	}else{
		triggertable_finishsweep(tt);
	}
	return true;
}

/*static void testfunc(struct triggertable * tt,struct flowentry * fe, uint32_t hash __attribute__((unused)), uint16_t size){
	uint16_t i;
	struct trigger * t;
	struct triggerflow *tf;
	for (i = 0; i < size; i++){
		t = (struct trigger *) tt->triggers_temp[i];
		tf = t->tfl->tf + t->tfhead_filled;
		t->tfl->fullmap |= ((uint64_t)1) << t->tfhead_filled;
		flow_fill(&tf->f, &fe->f);
		tf->fe = fe;
		tf->hash = hash;
		t->tfhead_filled++; 
	}

}*/

inline static void maketflforatrigger(struct triggertable * tt, struct trigger *t){
	struct triggerflowlist * tfl;
//	if (t->tfl == NULL){
	tfl = 
//MALLOC(sizeof(struct triggerflowlist)); 
	getnewflowlist(tt);
	tfl->next = t->tfl;
	tfl->fullmap = 0;
	t->tfl = tfl;
	t->tfhead_filled = 0;	
/*	else if (t->tfhead_filled == TRIGGERFLOW_BATCH){
		t->tfl->fullmap = 0;
		t->tfhead_filled = 0;*/
}

inline static void addflowtotrigger(struct trigger * t, struct flowentry * fe){
	t->matched++;
	struct triggerflow * tf;
	fe->summaries |= t->type->summarymask;
	tf = t->tfl->tf + t->tfhead_filled;
	t->tfl->fullmap |= ((uint64_t)1) << t->tfhead_filled;
	flow_fill(&tf->f, &fe->f);
//	tf->fe = fe;
//	tf->hash = hash;
	t->tfhead_filled++;
}

inline static void addflowtofgtrigger(struct triggertable * tt, struct trigger * t, struct flowentry * fe){
	//find trigger with similar event id
	t->matched++;
	uint16_t size = FLOWENTRY_TRIGGER_SIZE;
	struct trigger * t2;
	uint16_t i;
	matcher_matchmask(tt->m, &fe->f, trigger_fgmask(t), tt->triggers_temp, &size);
	bool found = false;
	for (i = 0; i < size; i++){
		t2 = (struct trigger *) tt->triggers_temp[i];
		if (t2->eventid == t->eventid){
			found = true;
			break;
		}
	}
	//if not found add it and add the flow to it	
	if (!found){
		struct trigger * t2 = triggertable_gettrigger(tt);
               	t2 = trigger_fginit(t, t2, &fe->f);
               	triggertable_addtrigger(tt, t2);
		addflowtotrigger(t2, fe);
	}
}

void triggertable_singletriggermatch(struct triggertable * tt, struct trigger * t, struct flowentry * fe, struct summary_table * st){
	if (t->tfl == NULL || t->tfhead_filled == TRIGGERFLOW_BATCH){
		maketflforatrigger(tt, t);
	}
	if (trigger_isfgmaster(t)){
		addflowtofgtrigger(tt, t, fe);
	}else{
		addflowtotrigger(t, fe);
	}
	summary_updatesummaries(st, fe);
	//FIXME: MESSES UP CURRENT SUMMARIES
}

//given a flowentry it will find and cache the result in the flowentry
void triggertable_sweepmatch(struct triggertable * tt, struct flowentry * fe, struct summary_table * st, uint32_t hash __attribute__((unused))){
	uint16_t size = FLOWENTRY_TRIGGER_SIZE;
	struct trigger * t;
	uint16_t i;
	matcher_match(tt->m, &fe->f, tt->triggers_temp, &size);
//	flow_print(&fe->f);
//	printf("%d\n", size);
	for (i = 0; i < size; i++){
		t = (struct trigger *) tt->triggers_temp[i];
		if (trigger_isfgmaster(t)) continue;
		if (t->tfl == NULL || t->tfhead_filled == TRIGGERFLOW_BATCH){
			maketflforatrigger(tt, t);
		}else{
			SWEEP_PREFETCH0(t->tfl);
		}
	}
/*	for (i = 0; i < size; i++){
		t = (struct trigger *) tt->triggers_temp[i];
		SWEEP_PREFETCH0(t->tfl->tf + t->tfhead_filled);
	}*/

	for (i = 0; i < size; i++){
		t = (struct trigger *) tt->triggers_temp[i];
		
		if (trigger_isfgmaster(t)){
			addflowtofgtrigger(tt, t, fe);
		}else{
			addflowtotrigger(t, fe);
		}
	//	rte_prefetch2(t->tfl->tf + t->tfhead_filled);
	}

//	testfunc(tt, fe, hash, size);

	summary_updatesummaries(st, fe);
}


#else
void triggertable_sweepmatch(struct triggertable * tt __attribute__((unused)), struct flowentry * fe __attribute__((unused)), struct summary_table * st __attribute__((unused)), uint32_t hash __attribute__((unused))){
}
uint16_t readtfl(struct triggertable * tt __attribute__((unused)), struct trigger * t __attribute__((unused))){return 0;};
void updatetickspertype(struct triggertable * tt __attribute__((unused)), uint32_t sweeptime __attribute__((unused)), uint32_t totalflow __attribute__((unused))){}
bool triggertable_sweep(struct triggertable * tt __attribute__((unused)), uint32_t sweeptime __attribute__((unused)), const uint32_t minsweepticks __attribute__((unused))){return false;}


void singletriggermatch(struct triggertable * tt, struct trigger * t, struct flowentry * fe, struct summary_table * st){
	uint16_t trigger_index = FLOWENTRY_TRIGGER_SIZE;
	for (i = 0; i < FLOWENTRY_TRIGGER_SIZE; i++){
		if (fe->triggers[i] == 0){
			trigger_index = i;
			break;
		}
	}
	if (trigger_index >= FLOWENTRY_TRIGGER_SIZE){
		fprintf(stderr, "Triggertable: Cannot add trigger %d to flow \n", t->id);
		return;
	}
	fe->triggers[trigger_index] = trigger_getpos(tt, t);
	fe->summaries |= t->type->summarymask;
       	trigger_index++;

	if (trigger_index < FLOWENTRY_TRIGGER_SIZE){
		fe->triggers[trigger_index] = 0;
	}

	summary_updatesummaries(st, fe);
}

//given a flowentry it will find and cache the result in the flowentry
void triggertable_match(struct triggertable * tt, struct flowentry * fe, struct summary_table * st){
	struct trigger * t;
	uint16_t i;
	uint16_t trigger_index = 0;
	uint16_t size = FLOWENTRY_TRIGGER_SIZE;
	matcher_match(tt->m, &fe->f, tt->triggers_temp, &size);

	for (i = 0; i < size; i++){
		t = (struct trigger *) tt->triggers_temp[i];
		fe->triggers[trigger_index] = trigger_getpos(tt, t);
		fe->summaries |= t->type->summarymask;
       		trigger_index++;
	}

	if (trigger_index < FLOWENTRY_TRIGGER_SIZE){
		fe->triggers[trigger_index] = 0;
	}

	summary_updatesummaries(st, fe);
}

void triggertable_update2(struct triggertable * tt, struct flowentry * fe, struct flatreport_pkt * pkt){
	int i, j;
	struct trigger * t;
	struct trigger * triggers [FLOWENTRY_TRIGGER_SIZE];

	if (fe == NULL){
		struct flowentry fe2;
		fe = &fe2;
	//	memset(fe, 0, sizeof(struct flowentry));
		triggertable_match(tt, fe, flatreport_getsummarytable(tt->fr));
	}
	SWEEP_PREFETCH0(fe->triggers);

	for (j = 0; (j < FLOWENTRY_TRIGGER_SIZE) && (fe->triggers[j] != 0); j++){
		triggers[j] = getTrigger(tt->position_table, fe->triggers[j]);
	}
	SWEEP_PREFETCH0(triggers[0]);	
	for (i = 0; i < j; i++){
		if (i < j-1){
                	SWEEP_PREFETCH0(triggers[i+1]);
                }
		t = triggers[i];

		//lazy reset
		if (t->lastreset - tt->fr->step > t->reset_interval){
			t->type->reset_func(t, tt);
			t->lastreset = tt->fr->step;
		}	
			
		// now update
		t->type->update_func(t, pkt, tt);
		//bool needReport = t->type->update_func(t, pkt, tt);
		
		//set for reporting if necessary
/*		if (!t->reported && needReport){
			t->reported = true;
			triggertable_addviolatedtrigger(tt, t);
		}*/
			
	}
}



//give a list of triggers and it will update them based on a flowentry
void triggertable_update(struct triggertable * tt, struct flowentry * fe, struct summary_table *st __attribute__((unused))){
	int i, j;
	struct trigger * t;
//	summarymask_t b;	
//	uint16_t buf_pos = 0;
	SWEEP_PREFETCH0(fe->triggers);
	/*for (i = 0, b = fe->summaries; b > 0; i++, b >>= 1){
		if (b & 0x01){
			tt->summary_pos_temp[i] = buf_pos;
			buf_pos += st->summaries[i]->size;
		}
	}	*/
	struct trigger * triggers [FLOWENTRY_TRIGGER_SIZE];

	for (j = 0; (j < FLOWENTRY_TRIGGER_SIZE) && (fe->triggers[j] != 0); j++){
		triggers[j] = getTrigger(tt->position_table, fe->triggers[j]);
	}
	SWEEP_PREFETCH0(triggers[0]);	
	for (i = 0; i < j; i++){
		if (i < j-1){
                        SWEEP_PREFETCH0(triggers[i+1]);
                }
		t = triggers[i];

		//lazy reset
		if (t->lastreset - tt->fr->step > t->reset_interval){
			t->type->reset_func(t, tt);
			t->lastreset = tt->fr->step;
		}	
			
		// now update
		t->type->update_func(t, fe, tt);
		bool needReport = t->type->condition_func(t);
			
		//set for reporting if necessary
		if (!t->reported && needReport){
			t->reported = true;
			triggertable_addviolatedtrigger(tt, t);
		}
			
	}
}


/*
* The old code that is used for the TCP (burst loss example)
*/
void triggertable_report(struct triggertable * tt){
	int i;
//	struct flatreport * fr = tt->fr;
	for (i = 0; i < tt->violated_entry_index; i++){
		struct trigger * t = *((tt->violated_entries_buffer) + i);
//		if (fr->trigger_report_buffer_current <= fr->trigger_report_buffer + trigger_report_buffer_size - sizeof(struct triggerreport)){
//			struct triggerreport * tr = (struct triggerreport *) fr->trigger_report_buffer_current;
			
			printf("OOOOOOOOOOOOOOOO!\n");
//			t->type->report_func(t, tr);
			t->reported = false;
			//FIXME		
//			printf("!!!!!!!!! vioilated\n");
			if (!acted){
				if(system("tc qd add dev eth4 root fq")){
					fprintf(stderr, "error in calling system\n");
				}
				//system("tc qdisc add dev eth4 handle 1: root htb");
				//system("tc class add dev eth4 parent 1: classid 1:1 htb rate 90mbit");
				acted=1;
			}
//			triggerreport_print(tr);	
			continue;
			
/*		}else{
			fprintf (stderr, "triggertable: report buffer is full\n");
			break;
		}*/
	}		
	tt->violated_entry_index = 0;
}

void triggertable_addviolatedtrigger(struct triggertable * tt, struct trigger * t){
	*((tt->violated_entries_buffer)+tt->violated_entry_index) = t;
	if (tt->violated_entry_index + 1 >= VIOLATED_ENTRIES_SIZE){
		fprintf(stderr, "not enough buffer for trigger report");
	}else{
		tt->violated_entry_index++;
	}
}


#endif

inline bool triggertable_issweepfinished(struct triggertable * tt){
	return tt->state.index == 0; //0 is unused
}

void triggertable_startsweep(struct triggertable * tt){
	struct sweep_state * state = &tt->state;
	state->seen = 0;
	state->tfl = NULL;
	state->tfl_last = NULL;
	state->index = 1;
	state->triggerinterrupted = false;
}


void triggertable_finishsweep(struct triggertable * tt){
	struct sweep_state * state = &tt->state;
	state->index = 0;
	struct triggertype * type;

	for (type = tt->types; type != NULL; type = type->next){
		if (type->ticksperupdate_num > 0){
			type->ticksperupdate = type->ticksperupdate/2 + type->ticksperupdate2 / 2 / type->ticksperupdate_num;	
			//x+= type->ticksperupdate_num;
			type->ticksperupdate_num = 0;
			type->ticksperupdate2 = 0;
		}
	}
}



void triggertable_print(struct triggertable * tt __attribute__((unused))){
	/*struct table * tbl = tt->tables;
	while (tbl != NULL){
		hashmap_apply(tbl->map, trigger_print, NULL);
		tbl = tbl->next;
	}*/
}


///////////////////////////////// TRIGGER TYPE /////////////////////////
struct triggertype * triggertype_init(uint16_t id, trigger_update_func update_func, trigger_report_func report_func, trigger_apply_func	free_func, trigger_apply_func reset_func, trigger_apply_func print_func, struct summary ** s, int summarynum, trigger_condition_func condition_func, uint32_t ticksperupdate){
	struct triggertype * type = MALLOC(sizeof(struct triggertype));
	type->id = id;
	type->update_func = update_func;
	type->report_func = report_func;
	type->free_func = free_func;
	type->reset_func = reset_func;
	type->print_func = print_func;
	type->condition_func = condition_func;
	type->next = NULL;
	type->flow_micronum = 0;
	type->ticksperupdate = ticksperupdate;
	type->ticksperupdate2 = 0;;
	type->ticksperupdate_num = 0;
	type->s = summarynum > 0 ? MALLOC(sizeof (struct summary *)*summarynum): NULL;
	type->summarymask = 0;
	int i;
	for (i = 0; i < summarynum; i++){
		type->s[i] = s[i];
		type->summarymask |= 1<<s[i]->index;
	}
	return type;
}

void triggertype_finish(struct triggertype * type){
	if (type->s != NULL ) FREE (type->s);
	FREE (type);
}

bool triggertype_equal(struct triggertype * type1, struct triggertype * type2){
	return type1==NULL || type2==NULL || type1->id==type2->id;
}

void triggertype_print(struct triggertype * type){
	printf("%u", (unsigned)type->id);
}

///////////////////////////////// TRIGGER /////////////////////////

void trigger_init(uint16_t eventid, struct trigger * t, struct flow * filter, struct flow * mask, struct triggertype * type){
	flow_mask(&t->filter, filter, mask);
	if (flow_equal(filter, &t->filter)){
		t->eventid = eventid;
		//flow_fill(&t->filter, filter);
		flow_fill(&t->mask, mask);
		//t->reported = false;
		t->lastreset = 0;
		t->type = type;
	}else{
		fprintf(stderr, "Filter doesn't match the mask\n");
		flow_print(filter);
		flow_print(&t->filter);
		printf("mask: ");
		flow_print(mask);
	}
}

void trigger_finish2(struct trigger * data, void * aux){
	trigger_finish(data, (struct triggertable *) aux);
}

void trigger_cleantfl(struct trigger * t __attribute__((unused)), struct triggertable * tt __attribute__((unused))){
#if TRIGGERTABLE_SWEEP
	if (t->tfl != NULL){
		struct triggerflowlist * oldhead = tt->freelist;
		tt->freelist = t->tfl; //this is now the new list head
		//go to the last entry of this tfl list
		for (;t->tfl->next != NULL; t->tfl = t->tfl->next){
		}
		t->tfl->next = oldhead;
	}
	t->tfl = NULL;
#endif
}

//don't do this recursive
void trigger_finish(struct trigger * t, struct triggertable * tt){
	t->type->free_func(t, NULL);
	trigger_cleantfl(t, tt);

#if TRIGGERTABLE_INLINE_TRIGGER
	memset(t, 0, sizeof(struct trigger));
#else
	FREE(t);
#endif
}

bool trigger_equal (void * data1, void * data2){
	struct trigger * t1 = (struct trigger *) data1;
	struct trigger * t2 = (struct trigger *) data2;	
	return t1->eventid == t2->eventid && flow_equal(&t1->mask, &t2->mask) && flow_equal(&t1->filter, &t2->filter)
	&& triggertype_equal(t1->type, t2->type);
}

// this should be useless if we use hashing
bool trigger_match(struct trigger * t, struct flow * f, struct flow * tempflow){
	flow_mask(tempflow, f, &t->mask);
	return flow_equal(tempflow, &t->filter);
}

void trigger_print(struct trigger * t, void * aux __attribute__((unused))){
	char buf [200];
	flow_inlineprint2(&t->filter, buf);
	LOG("%s ", buf);
	flow_inlineprint2(&t->mask, buf);
	LOG("%s ", buf);
	t->type->print_func(t, buf);
	LOG("%u (%u)  %"PRIu32" %s", t->eventid, t->id, t->matched, buf);
}

void trigger_print2(struct trigger * t, void * aux __attribute__((unused))){
	printf("id %u, filter ", (unsigned) t->id);
	flow_inlineprint(&t->filter);
	printf(", mask ");
	flow_inlineprint(&t->mask);
	printf(", type ");
	triggertype_print(t->type);
	char buf [30];
	t->type->print_func(t, buf);
	printf("%s\n", buf);
}

inline bool triggertable_getreport(struct triggertable * tt, struct trigger * t, char * buf, uint32_t time){
	if (tt->fr->step < time){
		fprintf(stderr, "Asked for time %d > step %d\n", time, tt->fr->step);
		return false;
	}
	return t->type->report_func(t, tt->fr->step - time, buf);
}


inline uint16_t trigger_getpos(struct triggertable * tt __attribute__((unused)), struct trigger *t){
#if TRIGGERTABLE_INLINE_TRIGGER
	return (t - tt->position_table)/sizeof(struct trigger);
#else
	return t->pos;
#endif
}

///////////////////////////////// TRIGGER INSTANCES /////////////////////////
#define TRIGGER_CURRENTHISTORY(t) ((uintptr_t)t->buf+t->historyindex)
void counter_trigger_setthreshold(struct trigger * t, uint32_t threshold);
void counter_trigger_setvalue(struct trigger * t, uint32_t value);
uint32_t counter_trigger_getthreshold(struct trigger * t);
uint32_t counter_trigger_getvalue(struct trigger * t);
uint32_t counter_trigger_addvalue(struct trigger * t, uint32_t v);

void counter_trigger_setthreshold(struct trigger * t, uint32_t threshold){
//	memcpy(t->buf, &threshold, sizeof(uint32_t);
	uint32_t * b2 = (uint32_t *)t->buf;
	*b2 = threshold;
}

inline void counter_trigger_setvalue(struct trigger * t, uint32_t value){
	*((uint32_t *)TRIGGER_CURRENTHISTORY(t)) = value;
}

inline uint32_t counter_trigger_addvalue(struct trigger * t, uint32_t v){
	uint32_t * value = (uint32_t *)TRIGGER_CURRENTHISTORY(t); 
	*value += v;
	return *value; 
}


inline uint32_t counter_trigger_getthreshold(struct trigger * t){
	uint32_t * b2 = (uint32_t *)t->buf;
	return *b2;
}


inline uint32_t counter_trigger_getvalue(struct trigger * t){
	return *((uint32_t *)TRIGGER_CURRENTHISTORY(t));
}


struct trigger * counter_trigger_init(struct trigger * t, uint16_t eventid, struct flow * filter, struct flow * mask, struct triggertype * type, uint32_t threshold, uint16_t timeinterval){
	trigger_init(eventid, t, filter, mask, type);
	counter_trigger_setthreshold(t, threshold);
	t->historyindex = 4; //reserve 4 for threshold;
	counter_trigger_setvalue(t, 0);
	t->reset_interval = timeinterval;
	if (t->reset_interval == 0 ){
		t->reset_interval = 1;
	}
	return t;
}

void counter_trigger_free(struct trigger * t __attribute__((unused)), void * aux __attribute__((unused))){
}


inline bool counter_trigger_condition(struct trigger * t){	
	return counter_trigger_getvalue(t) >= counter_trigger_getthreshold(t);
}

inline void counter_trigger_update(struct trigger * t, uint32_t v, struct triggertable *tt __attribute__((unused))){	
	counter_trigger_addvalue(t, v);
}

bool counter_trigger_report(struct trigger * t, uint32_t stepsback, char * buf){
	if (stepsback * 4 > sizeof(t->buf)-4){ // reserve 4 bytes for threshold
		fprintf(stderr, "Triggertable: too far step back %d\n", stepsback);
		return false;
	}
	int16_t h = t->historyindex - stepsback * 4;
	if (h <= 0){
		h += (sizeof(t->buf)/4 - 1) * 4;
	}
	uint32_t * b2 = (uint32_t *) buf;
	uint32_t * b1 = (uint32_t *)(t->buf + h);
	*b2 = *b1;
	//printf("%d\n", *b2);
	return true;
}

void counter_trigger_reset(struct trigger * t, void * aux __attribute__((unused))){
	t->historyindex += 4;
	if (t->historyindex + 4 > (uint8_t)sizeof(t->buf)){
		t->historyindex = 4;
	}
	t->historyindex = t->historyindex % sizeof(t->buf);
	counter_trigger_setvalue(t, 0);
}

void counter_trigger_print(struct trigger * t, void * aux){	
	sprintf((char *) aux, " value %"PRIu32" threshold %"PRIu32"\n", counter_trigger_getvalue(t), counter_trigger_getthreshold(t));
}

void counter_trigger_historyprint(struct trigger * t){
	uint8_t h = t->historyindex ;
	do{
		h += 4;
		if (t->historyindex + 4 > (uint8_t)sizeof(t->buf)){
			t->historyindex = 4;
		}
		printf("%d, ", *((uint32_t*)((uintptr_t)t->buf + h)));
	}while (h != t->historyindex);
	printf("\n");
}

void volume_trigger_update(struct trigger * t, void * data, struct triggertable * tt){
	uint32_t volume;
#if  PACKETHISTORY
	struct flatreport_pkt * pkt = (struct flatreport_pkt *) data; 
	volume = pkt->length;
#else
	struct flowentry * fe = (struct flowentry *) data; 
	volume = summary_volume_get(fe->buf + fe->summary_pos[t->type->s[0]->index], tt->fr);
#endif	
	return counter_trigger_update(t, volume, tt);
}

void pktnum_trigger_update(struct trigger * t, void * data __attribute__((unused)), struct triggertable * tt){
	uint32_t pktnum;
#if PACKETHISTORY
	pktnum = 1;
#else
	struct flowentry * fe = (struct flowentry *) data; 
	pktnum = summary_pktnum_get(fe->buf + fe->summary_pos[t->type->s[0]->index], tt->fr);
#endif
	counter_trigger_update(t, pktnum, tt);
}

void lossnum_trigger_update(struct trigger * t, void * data, struct triggertable * tt){
#if PACKETHISTORY
	fprintf(stderr, "lossnum is not supported \n");
	return;
#endif
	struct flowentry * fe = (struct flowentry *) data; 
	uint32_t lossnum = summary_lossnum2_get(fe->buf + fe->summary_pos[t->type->s[0]->index], tt->fr);
//	uint32_t lossnum = summary_lossnum_get(fe->buf + tt->summary_pos_temp[t->type->s[0]->index], tt->fr);
//	flow_inlineprint(&fe->f);
//	printf(" %d %"PRIu32"\n", tt->fr->step, lossnum);
	return counter_trigger_update(t, lossnum, tt);
}


void congestion_trigger_update(struct trigger * t, void * data __attribute__((unused)), struct triggertable * tt){
	const int ackindex = t->type->s[0]->index ;
	const int synindex = t->type->s[1]->index ;
	struct flowentry * fe = (struct flowentry *) data;	
	struct flow f;
	f.srcip = fe->f.dstip;
	f.dstip = fe->f.srcip;
	f.ports = ((fe->f.ports & 0xffff)<<16) | (fe->f.ports>>16);
	f.protocol = fe->f.protocol;
	struct flowentry * fe2 = flatreport_getflowentry(tt->fr, &f);
	if (fe2 == NULL) return;

	uint32_t ack = summary_ack_get(fe2->buf + fe2->summary_pos[ackindex]);
	uint32_t lastack = summary_ack_get2(fe2->buf + fe2->summary_pos[ackindex]);
	uint32_t dup = summary_ack_getdup(fe2->buf + fe2->summary_pos[ackindex]);
	uint32_t lastsyn = summary_syn_get2(fe->buf + fe->summary_pos[synindex]);
	int32_t lastdataonair = lastsyn - lastack;
	int32_t ackedbytes = ack - lastack + dup;
	uint32_t v = 0; //data that is not acked
	if (lastdataonair > 0){
		if (ackedbytes > lastdataonair){
			v = 0;
		}else{
			v = 100 - 100 * ackedbytes/lastdataonair;
		}
	}
	LOG("%lu: a %lu al %lu ls %lu dup %lu: %d / %d = %d\n",  tt->fr->step, ack, lastack, lastsyn, dup, ackedbytes, lastdataonair, v);
	
	uint32_t * value = (uint32_t *)TRIGGER_CURRENTHISTORY(t); 
	if (*value < v){
		*value = v;
	}
}


struct burstloss_state{
	struct burstlisthead * blh;
	struct trigger * t;
	struct burstsearchstate state;
};

void burstloss_check(uint32_t seq, void * aux){
	struct burstloss_state * state = (struct burstloss_state *)aux;
	bool isburst = burstlisthead_isburst(state->blh, seq, &state->state);
	if (isburst){
//		printf("burstloss: %lu\n", (long unsigned)seq);
		counter_trigger_addvalue(state->t, 1);
	}
}

void burstloss_trigger_update(struct trigger * t, void * data, struct triggertable * tt __attribute__((unused))){
	struct flowentry * fe = (struct flowentry *) data; 
	struct burstlisthead * blh = summary_burstlist_get(fe->buf+fe->summary_pos[t->type->s[0]->index]);
	struct losslisthead * llh = summary_losslist_get(fe->buf+fe->summary_pos[t->type->s[1]->index]);
	burstlisthead_dump(blh);
	
	struct burstloss_state state;
	state.blh = blh;
	state.t = t;
	state.state.current = NULL;
	
	losslisthead_apply(llh, burstloss_check, &state);
//	printf("%llu burst loss\n", (long long unsigned)t2->value);

	// now check if it is violating the threshold
//	return counter_trigger_getvalue(t) >= counter_trigger_getthreshold(t);
}

/* --------------------------------- FG master --------------------------*/

inline bool trigger_isfgmaster(struct trigger * t){
	return t->type->id == TRIGGERTABLE_TYPE_FGID; 	
}

struct trigger * fgcounter_trigger_init(struct trigger * t, uint16_t eventid, struct flow * filter, struct flow * mask, struct triggertype * type, uint32_t flowgranularity, struct triggertype * triggertype, uint32_t threshold, uint16_t timeinterval){
        trigger_init(eventid, t, filter, mask, type);
        counter_trigger_setthreshold(t, threshold);
        t->historyindex = 4; //reserve 4 for threshold;
        t->reset_interval = timeinterval;
        if (t->reset_interval == 0 ){
                t->reset_interval = 1;
        }
	
	*(struct triggertype **)((uintptr_t)t->buf + t->historyindex) = triggertype;
	t->historyindex += sizeof(struct triggertype *);

	
	uint8_t srcip_len, dstip_len, srcport_len, dstport_len, protocol_len;
	flow_parseflowgranularity(flowgranularity, &srcip_len, &dstip_len, &srcport_len, &dstport_len, &protocol_len);
		
	struct flow * f =  (struct flow *)((uintptr_t)t->buf + t->historyindex);
	flow_makemask(f, srcip_len, dstip_len, srcport_len, dstport_len, protocol_len);
	f->srcip |= mask->srcip;
	f->dstip |= mask->dstip;
	f->ports |= mask->ports;
        f->protocol |= mask->protocol;

        return t;
}

inline struct flow * trigger_fgmask(struct trigger * t){
	return (struct flow *)((uintptr_t)t->buf + t->historyindex);	
}


void fgcounter_trigger_reset(struct trigger * t __attribute__((unused)), void * aux __attribute__((unused))){
}

void fgcounter_trigger_print(struct trigger * t, void * aux){
	flow_inlineprint2(trigger_fgmask(t), (char *) aux);
	sprintf((char *) aux, " threshold %"PRIu32"\n", counter_trigger_getthreshold(t));
}

bool fgcounter_trigger_condition(struct trigger * t __attribute__((unused))){
	return false;
}

struct trigger * trigger_fginit(struct trigger * t, struct trigger * t2, struct flow * f){
	struct flow f2;
	flow_mask(&f2, f, trigger_fgmask(t));
	struct triggertype * type = *(struct triggertype **)((uintptr_t)t->buf + 4); 
	t2 = counter_trigger_init(t2, t->eventid, &f2, trigger_fgmask(t), type, counter_trigger_getthreshold(t), t->reset_interval);
	return t2;
}

void fgcounter_trigger_update(struct trigger * t __attribute__((unused)), void * d __attribute__((unused)), struct triggertable * tt __attribute__((unused))){
}

bool fgcounter_trigger_report(struct trigger * t __attribute__((unused)), uint32_t stepsback __attribute__((unused)), char * buf __attribute__((unused))){
	return true;
}
